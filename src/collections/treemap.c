/**
 * @file treemap.c
 * 
 * First published under the GPLv2 license by Oracle (see https://github.com/openjdk/jdk, TreeMap.java).
 * This code was then modified and translated into C.
 */

#include "treemap.h"

#include <stdlib.h>
#include <string.h>
#include <memdup.h>

Treemap* treemap_init(compare_fn cmp) {
	Treemap* m = malloc(sizeof(*m));
	if(!m)
		return NULL;

	m->size = 0;
	m->root = NULL;
	m->cmp = cmp ? cmp : map_default_cmp;
	return m;
}

static void mapentry_destroy(_TreemapEntry* p) {
	if(!p)
		return;

	mapentry_destroy(p->left);
	mapentry_destroy(p->right);
	free(p->key);
	if(p->val != NULL)
		free(p->val);
	free(p);
}

void treemap_destroy(Treemap* m) {
	mapentry_destroy(m->root);
	free(m);
}

size_t treemap_size(Treemap* m) {
	return m->size;
}

void treemap_clear(Treemap* m) {
	mapentry_destroy(m->root);
	m->root = NULL;
}

#define BLACK (true)
#define RED (false)

static _TreemapEntry* mapentry_new(const void* key, size_t len_key, const void* val, size_t len_val, _TreemapEntry* parent) {
	_TreemapEntry* e = malloc(sizeof(*e));
	if(!e)
		return NULL;

	e->key = memdup(key, len_key);
	if(!e->key)
		goto err0;

	e->len_key = len_key;
	if(val != NULL) {
		e->val = memdup(val, len_val);
		if(!e->val)
			goto err1;
	}
	else
		e->val = NULL;
	e->len_val = len_val;

	e->left = NULL;
	e->right = NULL;
	e->parent = parent;
	e->color = BLACK;
	e->weight = 0;

	return e;

err1:
	free(e->key);
err0:
	free(e);
	return NULL;
}

static _TreemapEntry* treemap_get_entry(Treemap* m, const void* key, size_t len_key) {
	_TreemapEntry* p = m->root;

	while(p) {
		int cmp = m->cmp(key, len_key, p->key, p->len_key);
		if(cmp < 0)
			p = p->left;
		else if(cmp > 0)
			p = p->right;
		else
			return p;
	}

	return NULL;
}

void* treemap_get(Treemap* m, const void* key, size_t len_key, size_t* len_val) {
	_TreemapEntry* p = treemap_get_entry(m, key, len_key);
	if(!p) {
		if(len_val)
			*len_val = 0;
		return NULL;
	}

	if(len_val)
		*len_val = p->len_val;
	return p->val;
}

bool treemap_item(Treemap* m, const void* key, size_t len_key, MapItem* i) {
	_TreemapEntry* p = treemap_get_entry(m, key, len_key);
	if(!p)
		return false;

	i->key = p->key;
	i->len_key = p->len_key;
	i->val = p->val;
	i->len_val = p->len_val;
	return true;
}

static void mapentry_update_weight(_TreemapEntry* p, ssize_t delta) {
	while(p != NULL) {
		p->weight += delta;
		p = p->parent;
	}
}

#define COLOR_OF(p) (!(p) ? BLACK : (p)->color)
#define SET_COLOR(p, c) do { if(p) (p)->color = (c); } while(0)
#define PARENT_OF(p) (!(p) ? NULL : (p)->parent)
#define LEFT_OF(p) (!(p) ? NULL : (p)->left)
#define RIGHT_OF(p) (!(p) ? NULL : (p)->right)
#define WEIGHT_OF(p) ((ssize_t) (!(p) ? 0 : (p)->weight))

static void mapentry_rotate_left(Treemap* m, _TreemapEntry* p) {
	_TreemapEntry* r;
	ssize_t delta;

	if(p != NULL) {
		r = p->right;

		delta = WEIGHT_OF(r->left) - WEIGHT_OF(p->right);
		p->right = r->left;
		mapentry_update_weight(p, delta);

		if(r->left != NULL)
			r->left->parent = p;

		r->parent = p->parent;

		if(p->parent == NULL)
			m->root = r;
		else if(p->parent->left == p) {
			delta = WEIGHT_OF(r) - WEIGHT_OF(p->parent->left);
			p->parent->left = r;
			mapentry_update_weight(p->parent, delta);
		}
		else {
			delta = WEIGHT_OF(r) - WEIGHT_OF(p->parent->right);
			p->parent->right = r;
			mapentry_update_weight(p->parent, delta);
		}

		delta = WEIGHT_OF(p) - WEIGHT_OF(r->left);
		r->left = p;
		mapentry_update_weight(r, delta);

		p->parent = r;
	}
}

static void mapentry_rotate_right(Treemap* m, _TreemapEntry* p) {
	_TreemapEntry* l;
	ssize_t delta;

	if(p != NULL) {
		l = p->left;

		delta = WEIGHT_OF(l->right) - WEIGHT_OF(p->left);
		p->left = l->right;
		mapentry_update_weight(p, delta);

		if(l->right != NULL)
			l->right->parent = p;

		l->parent = p->parent;

		if(p->parent == NULL)
			m->root = l;
		else if(p->parent->right == p) {
			delta = WEIGHT_OF(l) - WEIGHT_OF(p->parent->right);
			p->parent->right = l;
			mapentry_update_weight(p->parent, delta);
		}
		else {
			delta = WEIGHT_OF(l) - WEIGHT_OF(p->parent->left);
			p->parent->left = l;
			mapentry_update_weight(p->parent, delta);
		}

		delta = WEIGHT_OF(p) - WEIGHT_OF(l->right);
		l->right = p;
		mapentry_update_weight(l, delta);

		p->parent = l;
	}
}

static void treemap_fix_after_insert(Treemap* m, _TreemapEntry* x) {
	_TreemapEntry* y;

	x->color = RED;

	while(x != NULL && x != m->root && x->parent->color == RED) {
		if(PARENT_OF(x) == LEFT_OF(PARENT_OF(PARENT_OF(x)))) {
			y = RIGHT_OF(PARENT_OF(PARENT_OF(x)));
			if(COLOR_OF(y) == RED) {
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(y, BLACK);
				SET_COLOR(PARENT_OF(PARENT_OF(x)), RED);
				x = PARENT_OF(PARENT_OF(x));
			}
			else {
				if(x == RIGHT_OF(PARENT_OF(x))) {
					x = PARENT_OF(x);
					mapentry_rotate_left(m, x);
				}
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(PARENT_OF(PARENT_OF(x)), RED);
				mapentry_rotate_right(m, PARENT_OF(PARENT_OF(x)));
			}
		}
		else {
			y = LEFT_OF(PARENT_OF(PARENT_OF(x)));
			if(COLOR_OF(y) == RED) {
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(y, BLACK);
				SET_COLOR(PARENT_OF(PARENT_OF(x)), RED);
				x = PARENT_OF(PARENT_OF(x));
			}
			else {
				if(x == LEFT_OF(PARENT_OF(x))) {
					x = PARENT_OF(x);
					mapentry_rotate_right(m, x);
				}
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(PARENT_OF(PARENT_OF(x)), RED);
				mapentry_rotate_left(m, PARENT_OF(PARENT_OF(x)));
			}
		}
	}

	m->root->color = BLACK;
}

int treemap_put(Treemap* m, const void* key, size_t len_key, const void* val, size_t len_val) {
	if(!key)
		return -1;

	if(val == NULL)
		len_val = 0;

	_TreemapEntry* t = m->root;
	if(!t) {
		m->root = mapentry_new(key, len_key, val, len_val, NULL);
		if(!m->root)
			return -1;

		m->root->weight = 1;
		m->size = 1;
		return 0;
	}

	int cmp;
	_TreemapEntry* parent;

	do {
		parent = t;
		cmp = m->cmp(key, len_key, t->key, t->len_key);
		if(cmp < 0)
			t = t->left;
		else if(cmp > 0)
			t = t->right;
		else {
			void* tmp;
			if(val != NULL) {
				tmp = memdup(val, len_val);
				if(!tmp)
					return -1;
			}
			else
				tmp = NULL;

			if(t->val != NULL)
				free(t->val);
			t->val = tmp;
			t->len_val = len_val;
			return 1; // element replaced
		}
	} while(t != NULL);

	_TreemapEntry* e = mapentry_new(key, len_key, val, len_val, parent);
	if(cmp < 0)
		parent->left = e;
	else
		parent->right = e;

	mapentry_update_weight(e, 1);

	treemap_fix_after_insert(m, e);
	m->size++;

	return 0;
}

static _TreemapEntry* mapentry_successor(_TreemapEntry* t) {
	_TreemapEntry* p;

	if(t == NULL)
		return NULL;
	else if(t->right != NULL) {
		p = t->right;
		while(p->left != NULL)
			p = p->left;
		return p;
	}
	else {
		p = t->parent;
		_TreemapEntry* ch = t;
		while(p != NULL && ch == p->right) {
			ch = p;
			p = p->parent;
		}
		return p;
	}
}

static void treemap_fix_after_delete(Treemap* m, _TreemapEntry* x) {
	_TreemapEntry* sib;

	while(x != m->root && COLOR_OF(x) == BLACK) {
		if(x == LEFT_OF(PARENT_OF(x))) {
			sib = RIGHT_OF(PARENT_OF(x));

			if(COLOR_OF(sib) == RED) {
				SET_COLOR(sib, BLACK);
				SET_COLOR(PARENT_OF(x), RED);
				mapentry_rotate_left(m, PARENT_OF(x));
				sib = RIGHT_OF(PARENT_OF(x));
			}

			if(COLOR_OF(LEFT_OF(sib)) == BLACK && COLOR_OF(RIGHT_OF(sib)) == BLACK) {
				SET_COLOR(sib, RED);
				x = PARENT_OF(x);
			}
			else {
				if(COLOR_OF(RIGHT_OF(sib)) == BLACK) {
					SET_COLOR(LEFT_OF(sib), BLACK);
					SET_COLOR(sib, RED);
					mapentry_rotate_right(m, sib);
					sib = RIGHT_OF(PARENT_OF(x));
				}
				SET_COLOR(sib, COLOR_OF(PARENT_OF(x)));
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(RIGHT_OF(sib), BLACK);
				mapentry_rotate_left(m, PARENT_OF(x));
				x = m->root;
			}
		}
		else {
			sib = LEFT_OF(PARENT_OF(x));

			if(COLOR_OF(sib) == RED) {
				SET_COLOR(sib, BLACK);
				SET_COLOR(PARENT_OF(x), RED);
				mapentry_rotate_right(m, PARENT_OF(x));
				sib = LEFT_OF(PARENT_OF(x));
			}

			if(COLOR_OF(RIGHT_OF(sib)) == BLACK && COLOR_OF(LEFT_OF(sib)) == BLACK) {
				SET_COLOR(sib, RED);
				x = PARENT_OF(x);
			}
			else {
				if(COLOR_OF(LEFT_OF(sib)) == BLACK) {
					SET_COLOR(RIGHT_OF(sib), BLACK);
					SET_COLOR(sib, RED);
					mapentry_rotate_left(m, sib);
					sib = LEFT_OF(PARENT_OF(x));
				}
				SET_COLOR(sib, COLOR_OF(PARENT_OF(x)));
				SET_COLOR(PARENT_OF(x), BLACK);
				SET_COLOR(LEFT_OF(sib), BLACK);
				mapentry_rotate_right(m, PARENT_OF(x));
				x = m->root;
			}
		}
	}

	SET_COLOR(x, BLACK);
}

static void treemap_delete_entry(Treemap* m, _TreemapEntry* p) {
	m->size--;

	bool free_data = true;
	if(p->left != NULL && p->right != NULL) {
		_TreemapEntry* s = mapentry_successor(p);
		free(p->key);
		if(p->val != NULL)
			free(p->val);

		p->key = s->key;
		p->len_key = s->len_key;
		p->val = s->val;
		p->len_val = s->len_val;
		p = s;

		free_data = false;
	}

	_TreemapEntry* replacement = p->left != NULL ? p->left : p->right;

	ssize_t delta;
	if(replacement != NULL) {
		replacement->parent = p->parent;
		if(p->parent == NULL)
			m->root = replacement;
		else if(p == p->parent->left) {
			delta = WEIGHT_OF(replacement) - WEIGHT_OF(p->parent->left);
			p->parent->left = replacement;
			mapentry_update_weight(p->parent, delta);
		}
		else {
			delta = WEIGHT_OF(replacement) - WEIGHT_OF(p->parent->right);
			p->parent->right = replacement;
			mapentry_update_weight(p->parent, delta);
		}

		p->left = p->right = p->parent = NULL;

		if(p->color == BLACK)
			treemap_fix_after_delete(m, replacement);
	}
	else if(p->parent == NULL)
		m->root = NULL;
	else {
		if(p->color == BLACK)
			treemap_fix_after_delete(m, p);

		if(p->parent != NULL) {
			if(p == p->parent->left)
				p->parent->left = NULL;
			else if(p == p->parent->right)
				p->parent->right = NULL;

			mapentry_update_weight(p->parent, -1);

			p->parent = NULL;
		}
	}

	if(free_data) {
		free(p->key);
		if(p->val != NULL)
			free(p->val);
	}

	free(p);
}

int treemap_remove(Treemap* m, const void* key, size_t len_key) {
	_TreemapEntry* p = treemap_get_entry(m, key, len_key);
	if(!p)
		return 0;

	treemap_delete_entry(m, p);
	return 1;
}

bool treemap_contains_key(Treemap* m, const void* key, size_t len_key) {
	return treemap_get_entry(m, key, len_key) != NULL;
}

ssize_t treemap_index_of(Treemap* m, const void* key, size_t len_key) {
	if(!key)
		return -1;

	_TreemapEntry* e = treemap_get_entry(m, key, len_key);
	if(!e)
		return -1;

	if(e == m->root)
		return WEIGHT_OF(e) - WEIGHT_OF(e->right) - 1;

	_TreemapEntry* p = e->parent;
	size_t index = WEIGHT_OF(e->left);

	while(p != NULL) {
		if(m->cmp(key, len_key, p->key, p->len_key) > 0)
			index += WEIGHT_OF(p->left) + 1;
		p = p->parent;
	}

	return index;
}

static _TreemapEntry* mapentry_at_index(_TreemapEntry* e, size_t index) {
	if(e->left == NULL && index == 0)
		return e;

	if(e->left == NULL && e->right == NULL)
		return e;

	if(e->left != NULL && e->left->weight > index)
		return mapentry_at_index(e->left, index);

	if(e->left != NULL && e->left->weight == index)
		return e;

	return mapentry_at_index(e->right, index - (e->left == NULL ? 0 : e->left->weight) - 1);
}

bool treemap_get_item_at_index(Treemap* m, size_t index, MapItem* i) {
	if(index >= m->size)
		return false;

	_TreemapEntry* p = mapentry_at_index(m->root, index);
	if(!p)
		return false;

	i->key = p->key;
	i->len_key = p->len_key;
	i->val = p->val;
	i->len_val = p->len_val;
	return true;
}

void treemap_iter(Treemap* m, TreemapIterator* it) {
	_TreemapEntry* p = m->root;

	// getting first entry
	if(p != NULL)
		while(p->left != NULL)
			p = p->left;

	it->next = p;
}

bool treemap_iter_next(TreemapIterator* it, MapItem* i) {
	_TreemapEntry* e = it->next;
	if(e == NULL)
		return false;

	it->next = mapentry_successor(e);

	i->key = e->key;
	i->len_key = e->len_key;
	i->val = e->val;
	i->len_val = e->len_val;
	return true;
}

const void* treemap_iter_next_key(TreemapIterator* it, size_t* len_key) {
	_TreemapEntry* e = it->next;
	if(e == NULL) {
		if(len_key)
			*len_key = 0;
		return NULL;
	}

	it->next = mapentry_successor(e);

	if(len_key)
		*len_key = e->len_key;
	return e->key;
}
