/**
 * @file hashmap.c
 * 
 * First published under the GPLv2 license by Oracle (see https://github.com/openjdk/jdk, HashMap.java).
 * This code was then modified and translated into C.
 */

#include "hashmap.h"

#include <stdlib.h>
#include <memdup.h>

#define DEFAULT_INITIAL_CAPACITY (1 << 4)
#define MAXIMUM_CAPACITY (1 << 30)
#define DEFAULT_LOAD_FACTOR (0.75f)
#define TREEIFY_THRESHOLD (8)
#define UNTREEIFY_THRESHOLD (6)
#define MIN_TREEIFY_CAPACITY (64)

static Hash default_hash(const void* k, size_t l) {
	Hash h = 1;
	for(size_t i = 0; i < l; i++)
		HASH_COMBINE(h, ((uint8_t*) k)[i]);
	return h;
}

Hashmap* hashmap_init(compare_fn cmp, hash_fn hash) {
	Hashmap* m = malloc(sizeof(*m));
	if(!m)
		return NULL;

	m->table_length = 0;
	m->table = NULL;
	m->size = 0;
	m->threshold = 0;

	m->cmp = cmp ? cmp : map_default_cmp;
	m->hash = hash ? hash : default_hash;
	return m;
}

static void mapnode_destroy(_HashmapNode* p) {
	if(!p)
		return;

	if(p->tree_node) {
		_HashmapTreeNode* t = (_HashmapTreeNode*) p;
		mapnode_destroy((_HashmapNode*) t->left);
		mapnode_destroy((_HashmapNode*) t->right);
	}
	else
		mapnode_destroy(p->next);

	free(p->key);
	if(p->val != NULL)
		free(p->val);
	free(p);
}

void hashmap_destroy(Hashmap* m) {
	if(m->table) {
		for(size_t i = 0; i < m->table_length; i++)
			mapnode_destroy(m->table[i]);
		free(m->table);
	}
	free(m);
}

size_t hashmap_size(Hashmap* m) {
	return m->size;
}

void hashmap_clear(Hashmap* m) {
	_HashmapNode **tab, *e;
	if((tab = m->table) != NULL && m->size > 0) {
		m->size = 0;
		size_t n = m->table_length;
		for(size_t i = 0; i < n; ++i) {
			if((e = tab[i]) != NULL) {
				mapnode_destroy(e);
				tab[i] = NULL;
			}
		}
	}
}

static _HashmapTreeNode* hashmap_treenode_root(_HashmapTreeNode* r) {
	_HashmapTreeNode* p;
	for(;;) {
		if((p = r->parent) == NULL)
			return r;
		r = p;
	}
}

static _HashmapTreeNode* hashmap_treenode_find(Hashmap* m, _HashmapTreeNode* p, Hash h, const void* key, size_t len_key) {
	_HashmapTreeNode *pl, *pr, *q;
	do {
		Hash ph;
		int dir;
		pl = p->left, pr = p->right;
		if((ph = p->hash) > h)
			p = pl;
		else if(ph < h)
			p = pr;
		else if((dir = m->cmp(key, len_key, p->key, p->len_key)) == 0)
			return p;
		else if(pl == NULL)
			p = pr;
		else if(pr == NULL)
			p = pl;
		else
			p = (dir < 0) ? pl : pr;
	}
	while(p != NULL);
	return NULL;
}

static _HashmapTreeNode* hashmap_treenode_get_tree_node(Hashmap* m, _HashmapTreeNode* n, Hash hash, const void* key, size_t len_key) {
	return hashmap_treenode_find(m, (n->parent != NULL) ? hashmap_treenode_root(n) : n, hash, key, len_key);
}

static inline Hash hash_val(Hashmap* m, const void* key, size_t len_key) {
	Hash h;
	if(key == NULL)
		return 0;

	h = m->hash(key, len_key);
	return h ^ (h >> 16);
}

static _HashmapNode* hashmap_get_node(Hashmap* m, const void* key, size_t len_key) {
	_HashmapNode **tab, *first, *e; size_t n; Hash hash;
	if((tab = m->table) != NULL && (n = m->table_length) > 0 &&
		(first = tab[(n - 1) & (hash = hash_val(m, key, len_key))]) != NULL) {
		if(first->hash == hash && // always check first node
			(key != NULL && m->cmp(key, len_key, first->key, first->len_key) == 0))
			return first;
		if((e = first->next) != NULL) {
			if(first->tree_node)
				return (_HashmapNode*) hashmap_treenode_get_tree_node(m, (_HashmapTreeNode*) first, hash, key, len_key);
			do {
				if(e->hash == hash &&
					(key != NULL && m->cmp(key, len_key, e->key, e->len_key) == 0))
					return e;
			}
			while((e = e->next) != NULL);
		}
	}
	return NULL;
}

void* hashmap_get(Hashmap* m, const void* key, size_t len_key, size_t* len_val) {
	_HashmapNode* e = hashmap_get_node(m, key, len_key);
	if(!e) {
		if(len_val)
			*len_val = 0;
		return NULL;
	}

	if(len_val)
		*len_val = e->len_val;
	return e->val;
}

bool hashmap_item(Hashmap* m, const void* key, size_t len_key, MapItem* i) {
	_HashmapNode* e = hashmap_get_node(m, key, len_key);
	if(!e)
		return false;

	i->key = e->key;
	i->len_key = e->len_key;
	i->val = e->val;
	i->len_val = e->len_val;
	return true;
}

static void hashmap_treenode_move_root_to_front(_HashmapNode** tab, size_t n, _HashmapTreeNode* root) {
	_HashmapTreeNode *first, *rp;
	_HashmapNode *rn;
	if(root != NULL && tab != NULL && n > 0) {
		size_t index = (n - 1) & root->hash;
		first = (_HashmapTreeNode*) tab[index];
		if(root != first) {
			tab[index] = (_HashmapNode*) root;
			rp = root->prev;
			if((rn = root->next) != NULL)
				((_HashmapTreeNode*) rn)->prev = rp;
			if(rp != NULL)
				rp->next = rn;
			if(first != NULL)
				first->prev = root;
			root->next = (_HashmapNode*) first;
			root->prev = NULL;
		}
	}
}

static _HashmapTreeNode* hashmap_treenode_rotate_left(_HashmapTreeNode* root, _HashmapTreeNode* p) {
	_HashmapTreeNode *r, *pp, *rl;
	if(p != NULL && (r = p->right) != NULL) {
		if((rl = p->right = r->left) != NULL)
			rl->parent = p;
		if((pp = r->parent = p->parent) == NULL)
			(root = r)->red = false;
		else if(pp->left == p)
			pp->left = r;
		else
			pp->right = r;
		r->left = p;
		p->parent = r;
	}
	return root;
}

static _HashmapTreeNode* hashmap_treenode_rotate_right(_HashmapTreeNode* root, _HashmapTreeNode* p) {
	_HashmapTreeNode *l, *pp, *lr;
	if(p != NULL && (l = p->left) != NULL) {
		if((lr = p->left = l->right) != NULL)
			lr->parent = p;
		if((pp = l->parent = p->parent) == NULL)
			(root = l)->red = false;
		else if(pp->right == p)
			pp->right = l;
		else
			pp->left = l;
		l->right = p;
		p->parent = l;
	}
	return root;
}

static _HashmapTreeNode* hashmap_treenode_balance_insertion(_HashmapTreeNode* root, _HashmapTreeNode* x) {
	x->red = true;
	for(_HashmapTreeNode *xp, *xpp, *xppl, *xppr;;) {
		if((xp = x->parent) == NULL) {
			x->red = false;
			return x;
		}
		else if(!xp->red || (xpp = xp->parent) == NULL)
			return root;
		if(xp == (xppl = xpp->left)) {
			if((xppr = xpp->right) != NULL && xppr->red) {
				xppr->red = false;
				xp->red = false;
				xpp->red = true;
				x = xpp;
			}
			else {
				if(x == xp->right) {
					root = hashmap_treenode_rotate_left(root, x = xp);
					xpp = (xp = x->parent) == NULL ? NULL : xp->parent;
				}
				if(xp != NULL) {
					xp->red = false;
					if(xpp != NULL) {
						xpp->red = true;
						root = hashmap_treenode_rotate_right(root, xpp);
					}
				}
			}
		}
		else {
			if(xppl != NULL && xppl->red) {
				xppl->red = false;
				xp->red = false;
				xpp->red = true;
				x = xpp;
			}
			else {
				if(x == xp->left) {
					root = hashmap_treenode_rotate_right(root, x = xp);
					xpp = (xp = x->parent) == NULL ? NULL : xp->parent;
				}
				if(xp != NULL) {
					xp->red = false;
					if(xpp != NULL) {
						xpp->red = true;
						root = hashmap_treenode_rotate_left(root, xpp);
					}
				}
			}
		}
	}
}

static void hashmap_treenode_treeify(_HashmapTreeNode* n, Hashmap* m, _HashmapNode** tab, size_t tab_len) {
	_HashmapTreeNode* root = NULL;
	_HashmapTreeNode *x, *next, *p, *xp;

	for(x = n; x != NULL; x = next) {
		next = (_HashmapTreeNode*) x->next;
		x->left = x->right = NULL;
		if(root == NULL) {
			x->parent = NULL;
			x->red = false;
			root = x;
		}
		else {
			size_t kl = x->len_key;
			Hash h = x->hash;
			for(p = root;;) {
				int dir, ph;
				if((ph = p->hash) > h)
					dir = -1;
				else if(ph < h)
					dir = 1;
				else
					dir = m->cmp(x->key, x->len_key, p->key, p->len_key);

				xp = p;
				if((p = (dir <= 0) ? p->left : p->right) == NULL) {
					x->parent = xp;
					if(dir <= 0)
						xp->left = x;
					else
						xp->right = x;
					root = hashmap_treenode_balance_insertion(root, x);
					break;
				}
			}
		}
	}
	hashmap_treenode_move_root_to_front(tab, tab_len, root);
}

static _HashmapNode* _hashmap_new_node(Hash hash, const void* key, size_t len_key, const void* val, size_t len_val, _HashmapNode* next, bool tree_node) {
	_HashmapNode* e = malloc(tree_node ? sizeof(_HashmapTreeNode) : sizeof(_HashmapNode));
	if(!e)
		return NULL;

	e->tree_node = tree_node;
	e->hash = hash;
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

	e->next = next;

	if(tree_node) {
		_HashmapTreeNode* t = (_HashmapTreeNode*) e;
		t->prev = NULL;
		t->parent = NULL;
		t->left = NULL;
		t->right = NULL;
		t->red = false;
	}

	return e;

err1:
	free(e->key);
err0:
	free(e);
	return NULL;
}

#define hashmap_new_node(h, k, lk, v, lv, next) _hashmap_new_node(h, k, lk, v, lv, next, false)
#define hashmap_new_treenode(h, k, lk, v, lv, next) ((_HashmapTreeNode*) _hashmap_new_node(h, k, lk, v, lv, next, true))

static inline _HashmapNode* hashmap_replacement_node(_HashmapNode* e, _HashmapNode* next) {
	e = realloc(e, sizeof(*e)); // realloc so the data does not have to be copied
	if(!e)
		return NULL;

	e->tree_node = false;
	e->next = next;
	return e;
}

static inline _HashmapTreeNode* hashmap_replacement_treenode(_HashmapNode* e, _HashmapNode* next) {
	_HashmapTreeNode* n = realloc(e, sizeof(*n)); // realloc so the data does not have to be copied
	if(!n)
		return NULL;

	n->tree_node = true;
	n->next = next;
	n->prev = NULL;
	n->parent = NULL;
	n->left = NULL;
	n->right = NULL;
	n->red = false;
	return n;
}

static _HashmapNode* hashmap_treenode_untreeify(_HashmapTreeNode* n, Hashmap* map) {
	_HashmapNode *hd = NULL, *tl = NULL, *q, *p, *next;

	q = (_HashmapNode*) n;
	while(q != NULL) {
		next = q->next;

		p = hashmap_replacement_node(q, NULL);
		if(tl == NULL)
			hd = p;
		else
			tl->next = p;
		tl = p;

		q = next;
	}
	return hd;
}

static void hashmap_treenode_split(_HashmapTreeNode* n, Hashmap* map, _HashmapNode** tab, size_t tab_len, size_t index, size_t bit) {
	_HashmapTreeNode* b = n;
	_HashmapTreeNode *lo_head = NULL, *lo_tail = NULL;
	_HashmapTreeNode *hi_head = NULL, *hi_tail = NULL;
	int lc = 0, hc = 0;

	_HashmapTreeNode *e, *next;
	for(e = b; e != NULL; e = next) {
		next = (_HashmapTreeNode*) e->next;
		e->next = NULL;
		if((e->hash & bit) == 0) {
			if((e->prev = lo_tail) == NULL)
				lo_head = e;
			else
				lo_tail->next = (_HashmapNode*) e;
			lo_tail = e;
			++lc;
		}
		else {
			if((e->prev = hi_tail) == NULL)
				hi_head = e;
			else
				hi_tail->next = (_HashmapNode*) e;
			hi_tail = e;
			++hc;
		}
	}

	if(lo_head != NULL) {
		if(lc <= UNTREEIFY_THRESHOLD)
			tab[index] = hashmap_treenode_untreeify(lo_head, map);
		else {
			tab[index] = (_HashmapNode*) lo_head;
			if(hi_head != NULL) // (else is already treeified)
				hashmap_treenode_treeify(lo_head, map, tab, tab_len);
		}
	}
	if(hi_head != NULL) {
		if(hc <= UNTREEIFY_THRESHOLD)
			tab[index + bit] = hashmap_treenode_untreeify(hi_head, map);
		else {
			tab[index + bit] = (_HashmapNode*) hi_head;
			if(lo_head != NULL)
				hashmap_treenode_treeify(hi_head, map, tab, tab_len);
		}
	}
}

static _HashmapNode** hashmap_resize(Hashmap* m, size_t* new_len) {
	_HashmapNode** old_tab = m->table;
	size_t old_cap = old_tab == NULL ? 0 : m->table_length;
	size_t old_thr = m->threshold;
	size_t new_cap, new_thr = 0;

	if(old_cap > 0) {
		if(old_cap >= MAXIMUM_CAPACITY) {
			m->threshold = SIZE_MAX;

			if(new_len)
				*new_len = old_cap;
			return old_tab;
		}
		else if((new_cap = old_cap << 1) < MAXIMUM_CAPACITY && old_cap >= DEFAULT_INITIAL_CAPACITY)
			new_thr = old_thr << 1;
	}
	else if(old_thr > 0)
		new_cap = old_thr;
	else {
		new_cap = DEFAULT_INITIAL_CAPACITY;
		new_thr = (size_t)(DEFAULT_LOAD_FACTOR * DEFAULT_INITIAL_CAPACITY);
	}
	if(new_thr == 0) {
		float ft = (float) new_cap * DEFAULT_LOAD_FACTOR;
		new_thr = (new_cap < MAXIMUM_CAPACITY && ft < (float)MAXIMUM_CAPACITY ? (size_t)ft : SIZE_MAX);
	}

	m->threshold = new_thr;

	_HashmapNode** new_tab = calloc(new_cap, sizeof(_HashmapNode*));
	m->table = new_tab;
	m->table_length = new_cap;

	if(old_tab != NULL) {
		for(size_t j = 0; j < old_cap; ++j) {
			_HashmapNode* e;
			if((e = old_tab[j]) != NULL) {
				old_tab[j] = NULL;
				if(e->next == NULL)
					new_tab[e->hash & (new_cap - 1)] = e;
				else if(e->tree_node)
					hashmap_treenode_split((_HashmapTreeNode*) e, m, new_tab, new_cap, j, old_cap);
				else { // preserve order
					_HashmapNode *lo_head = NULL, *lo_tail = NULL;
					_HashmapNode *hi_head = NULL, *hi_tail = NULL;
					_HashmapNode *next;
					do {
						next = e->next;
						if((e->hash & old_cap) == 0) {
							if(lo_tail == NULL)
								lo_head = e;
							else
								lo_tail->next = e;
							lo_tail = e;
						}
						else {
							if(hi_tail == NULL)
								hi_head = e;
							else
								hi_tail->next = e;
							hi_tail = e;
						}
					} while ((e = next) != NULL);
					if(lo_tail != NULL) {
						lo_tail->next = NULL;
						new_tab[j] = lo_head;
					}
					if(hi_tail != NULL) {
						hi_tail->next = NULL;
						new_tab[j + old_cap] = hi_head;
					}
				}
			}
		}
	}

	free(old_tab);
	if(new_len)
		*new_len = new_cap;
	return new_tab;
}

static _HashmapNode* hashmap_treenode_put_tree_val(_HashmapTreeNode* this, Hashmap* map, _HashmapNode** tab, size_t tab_len, Hash h, const void* key, size_t len_key, const void* val, size_t len_val) {
	bool searched = false;
	_HashmapTreeNode* root = (this->parent != NULL) ? hashmap_treenode_root(this) : this;
	_HashmapTreeNode* p;
	for(p = root;;) {
		int dir;
		Hash ph;
		if((ph = p->hash) > h)
			dir = -1;
		else if(ph < h)
			dir = 1;
		else if(key != NULL && (dir = map->cmp(key, len_key, p->key, p->len_key)) == 0)
			return (_HashmapNode*) p;

		_HashmapTreeNode* xp = p;
		if((p = (dir <= 0) ? p->left : p->right) == NULL) {
			_HashmapNode* xpn = xp->next;
			_HashmapTreeNode* x = hashmap_new_treenode(h, key, len_key, val, len_val, xpn);
			if(dir <= 0)
				xp->left = x;
			else
				xp->right = x;
			xp->next = (_HashmapNode*) x;
			x->parent = x->prev = xp;
			if(xpn != NULL)
				((_HashmapTreeNode*) xpn)->prev = x;

			hashmap_treenode_move_root_to_front(tab, tab_len, hashmap_treenode_balance_insertion(root, x));
			return NULL;
		}
	}
}

static void hashmap_treeify_bin(Hashmap* m, _HashmapNode** tab, size_t tab_len, Hash hash) {
	size_t n, index;
	_HashmapNode* e;
	if(tab == NULL || (n = tab_len) < MIN_TREEIFY_CAPACITY)
		hashmap_resize(m, NULL);
	else if((e = tab[index = (n - 1) & hash]) != NULL) {
		_HashmapTreeNode *hd = NULL, *tl = NULL, *p;
		_HashmapNode *next;
		do {
			next = e->next;

			p = hashmap_replacement_treenode(e, NULL);
			if(tl == NULL)
				hd = p;
			else {
				p->prev = tl;
				tl->next = (_HashmapNode*) p;
			}
			tl = p;
		} while((e = next) != NULL);
		if((tab[index] = (_HashmapNode*) hd) != NULL)
			hashmap_treenode_treeify(hd, m, tab, tab_len);
	}
}

// noops because these functions do not do anything at the original hashmap implementation
#define hashmap_after_node_access(m, e) ((void) 0)
#define hashmap_after_node_insertion(m, evict) ((void) 0)

static void hashmap_after_node_removal(Hashmap* m, _HashmapNode* e) {
	free(e->key);
	if(e->val)
		free(e->val);
	free(e);
}

int hashmap_put(Hashmap* m, const void* key, size_t len_key, const void* val, size_t len_val) {
	_HashmapNode **tab, *p;
	size_t n, i;

	if((tab = m->table) == NULL || (n = m->table_length) == 0)
		tab = hashmap_resize(m, &n);

	Hash hash = hash_val(m, key, len_key);
	if(val == NULL) // small modification for me
		len_val = 0;

	if((p = tab[i = (n - 1) & hash]) == NULL)
		tab[i] = hashmap_new_node(hash, key, len_key, val, len_val, NULL);
	else {
		_HashmapNode* e;
		if(p->hash == hash &&
			(key != NULL && m->cmp(key, len_key, p->key, p->len_key) == 0))
			e = p;
		else if(p->tree_node)
			e = hashmap_treenode_put_tree_val((_HashmapTreeNode*) p, m, tab, m->table_length, hash, key, len_key, val, len_val);
		else {
			for(int binCount = 0; ; ++binCount) {
				if((e = p->next) == NULL) {
					p->next = hashmap_new_node(hash, key, len_key, val, len_val, NULL);
					if(binCount >= TREEIFY_THRESHOLD - 1) // -1 for 1st
						hashmap_treeify_bin(m, tab, m->table_length, hash);
					break;
				}
				if(e->hash == hash &&
					(key != NULL && m->cmp(key, len_key, e->key, e->len_key) == 0))
					break;
				p = e;
			}
		}

		if(e != NULL) { // existing mapping for key
			void* tmp;
			if(val != NULL) {
				tmp = memdup(val, len_val);
				if(!tmp)
					return -1;
			}
			else
				tmp = NULL;

			if(e->val != NULL)
				free(e->val);
			e->val = tmp;
			e->len_val = len_val;
			hashmap_after_node_access(m, e);
			return 1;
		}
	}

	if(++m->size > m->threshold)
		hashmap_resize(m, NULL);
	hashmap_after_node_insertion(m, true);
	return 0;
}

static _HashmapTreeNode* hashmap_treenode_balance_deletion(_HashmapTreeNode* root, _HashmapTreeNode* x) {
	for(_HashmapTreeNode *xp, *xpl, *xpr;;) {
		if(x == NULL || x == root)
			return root;
		else if((xp = x->parent) == NULL) {
			x->red = false;
			return x;
		}
		else if(x->red) {
			x->red = false;
			return root;
		}
		else if((xpl = xp->left) == x) {
			if((xpr = xp->right) != NULL && xpr->red) {
				xpr->red = false;
				xp->red = true;
				root = hashmap_treenode_rotate_left(root, xp);
				xpr = (xp = x->parent) == NULL ? NULL : xp->right;
			}
			if(xpr == NULL)
				x = xp;
			else {
				_HashmapTreeNode *sl = xpr->left, *sr = xpr->right;
				if((sr == NULL || !sr->red) &&
					(sl == NULL || !sl->red)) {
					xpr->red = true;
					x = xp;
				}
				else {
					if(sr == NULL || !sr->red) {
						if(sl != NULL)
							sl->red = false;
						xpr->red = true;
						root = hashmap_treenode_rotate_right(root, xpr);
						xpr = (xp = x->parent) == NULL ?
							NULL : xp->right;
					}
					if(xpr != NULL) {
						xpr->red = (xp == NULL) ? false : xp->red;
						if((sr = xpr->right) != NULL)
							sr->red = false;
					}
					if(xp != NULL) {
						xp->red = false;
						root = hashmap_treenode_rotate_left(root, xp);
					}
					x = root;
				}
			}
		}
		else { // symmetric
			if(xpl != NULL && xpl->red) {
				xpl->red = false;
				xp->red = true;
				root = hashmap_treenode_rotate_right(root, xp);
				xpl = (xp = x->parent) == NULL ? NULL : xp->left;
			}
			if(xpl == NULL)
				x = xp;
			else {
				_HashmapTreeNode *sl = xpl->left, *sr = xpl->right;
				if((sl == NULL || !sl->red) &&
					(sr == NULL || !sr->red)) {
					xpl->red = true;
					x = xp;
				}
				else {
					if(sl == NULL || !sl->red) {
						if(sr != NULL)
							sr->red = false;
						xpl->red = true;
						root = hashmap_treenode_rotate_left(root, xpl);
						xpl = (xp = x->parent) == NULL ?
							NULL : xp->left;
					}
					if(xpl != NULL) {
						xpl->red = (xp == NULL) ? false : xp->red;
						if((sl = xpl->left) != NULL)
							sl->red = false;
					}
					if(xp != NULL) {
						xp->red = false;
						root = hashmap_treenode_rotate_right(root, xp);
					}
					x = root;
				}
			}
		}
	}
}

static void hashmap_treenode_remove_tree_node(_HashmapTreeNode* this, Hashmap* map, _HashmapNode** tab, size_t tab_len, bool movable) {
	size_t n;
	if(tab == NULL || (n = tab_len) == 0)
		return;
	size_t index = (n - 1) & this->hash;
	_HashmapTreeNode *first = (_HashmapTreeNode*) tab[index], *root = first, *rl;
	_HashmapTreeNode *succ = (_HashmapTreeNode*) this->next, *pred = this->prev;
	if(pred == NULL)
		tab[index] = (_HashmapNode*) (first = succ);
	else
		pred->next = (_HashmapNode*) succ;
	if(succ != NULL)
		succ->prev = pred;
	if(first == NULL)
		return;
	if(root->parent != NULL)
		root = hashmap_treenode_root(root);
	if(root == NULL
		|| (movable
			&& (root->right == NULL
				|| (rl = root->left) == NULL
				|| rl->left == NULL))) {
		tab[index] = hashmap_treenode_untreeify(first, map);  // too small
		return;
	}
	_HashmapTreeNode *p = this, *pl = this->left, *pr = this->right, *replacement;
	if(pl != NULL && pr != NULL) {
		_HashmapTreeNode *s = pr, *sl;
		while ((sl = s->left) != NULL) // find successor
			s = sl;
		bool c = s->red; s->red = p->red; p->red = c; // swap colors
		_HashmapTreeNode* sr = s->right;
		_HashmapTreeNode* pp = p->parent;
		if(s == pr) { // p was s's direct parent
			p->parent = s;
			s->right = p;
		}
		else {
			_HashmapTreeNode* sp = s->parent;
			if((p->parent = sp) != NULL) {
				if(s == sp->left)
					sp->left = p;
				else
					sp->right = p;
			}
			if((s->right = pr) != NULL)
				pr->parent = s;
		}
		p->left = NULL;
		if((p->right = sr) != NULL)
			sr->parent = p;
		if((s->left = pl) != NULL)
			pl->parent = s;
		if((s->parent = pp) == NULL)
			root = s;
		else if(p == pp->left)
			pp->left = s;
		else
			pp->right = s;
		if(sr != NULL)
			replacement = sr;
		else
			replacement = p;
	}
	else if(pl != NULL)
		replacement = pl;
	else if(pr != NULL)
		replacement = pr;
	else
		replacement = p;
	if(replacement != p) {
		_HashmapTreeNode* pp = replacement->parent = p->parent;
		if(pp == NULL)
			(root = replacement)->red = false;
		else if(p == pp->left)
			pp->left = replacement;
		else
			pp->right = replacement;
		p->left = p->right = p->parent = NULL;
	}

	_HashmapTreeNode* r = p->red ? root : hashmap_treenode_balance_deletion(root, replacement);

	if(replacement == p) {  // detach
		_HashmapTreeNode* pp = p->parent;
		p->parent = NULL;
		if(pp != NULL) {
			if(p == pp->left)
				pp->left = NULL;
			else if(p == pp->right)
				pp->right = NULL;
		}
	}
	if(movable)
		hashmap_treenode_move_root_to_front(tab, tab_len, r);
}

static int hashmap_remove_node(Hashmap* m, Hash hash, const void* key, size_t len_key, bool movable) {
	_HashmapNode **tab, *p; size_t n, index;
	if((tab = m->table) != NULL && (n = m->table_length) > 0 &&
		(p = tab[index = (n - 1) & hash]) != NULL) {
		_HashmapNode *node = NULL, *e;
		if(p->hash == hash &&
			(key != NULL && m->cmp(key, len_key, p->key, p->len_key) == 0))
			node = p;
		else if((e = p->next) != NULL) {
			if(p->tree_node)
				node = (_HashmapNode*) hashmap_treenode_get_tree_node(m, (_HashmapTreeNode*) p, hash, key, len_key);
			else {
				do {
					if(e->hash == hash &&
						(key != NULL && m->cmp(key, len_key, e->key, e->len_key) == 0)) {
						node = e;
						break;
					}
					p = e;
				}
				while((e = e->next) != NULL);
			}
		}
		if(node != NULL) {
			if(node->tree_node)
				hashmap_treenode_remove_tree_node((_HashmapTreeNode*) node, m, tab, n, movable);
			else if(node == p)
				tab[index] = node->next;
			else
				p->next = node->next;
			--m->size;
			hashmap_after_node_removal(m, node);
			return 1;
		}
	}
	return 0;
}

int hashmap_remove(Hashmap* m, const void* key, size_t len_key) {
	Hash hash = hash_val(m, key, len_key);
	return hashmap_remove_node(m, hash, key, len_key, true);
}

bool hashmap_contains_key(Hashmap* m, const void* key, size_t len_key) {
	return hashmap_get_node(m, key, len_key) != NULL;
}

void hashmap_iter(Hashmap* m, HashmapIterator* it) {
	it->map = m;
	it->current = it->next = NULL;
	it->index = 0;

	if(m->table != NULL && m->size > 0)
		do {} while (it->index < m->table_length && (it->next = m->table[it->index++]) == NULL);
}

static _HashmapNode* hashmap_iter_next_node(HashmapIterator* it) {
	_HashmapNode **t, *e;
	if((e = it->next) == NULL)
		return NULL;

	if((it->next = (it->current = e)->next) == NULL && (t = it->map->table) != NULL) {
		size_t n = it->map->table_length;
		do {} while (it->index < n && (it->next = t[it->index++]) == NULL);
	}

	return e;
}

bool hashmap_iter_next(HashmapIterator* it, MapItem* i) {
	_HashmapNode* e = hashmap_iter_next_node(it);
	if(e == NULL)
		return false;

	i->key = e->key;
	i->len_key = e->len_key;
	i->val = e->val;
	i->len_val = e->len_val;
	return true;
}

const void* hashmap_iter_next_key(HashmapIterator* it, size_t* len_key) {
	_HashmapNode* e = hashmap_iter_next_node(it);
	if(e == NULL) {
		if(len_key != NULL)
			*len_key = 0;
		return NULL;
	}

	if(len_key != NULL)
		*len_key = e->len_key;
	return e->key;
}

void hashmap_iter_remove(HashmapIterator* it) {
	_HashmapNode* p = it->current;
	if(p == NULL)
		return;

	it->current = NULL;
	hashmap_remove_node(it->map, p->hash, p->key, p->len_key, false);
}
