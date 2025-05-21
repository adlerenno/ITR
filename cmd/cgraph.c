/**
 * @file cgraph.c
 * @author FR
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>

#include <cgraph.h>
#include <constants.h>

#include "arith.h"

// used to convert the default values of the compression to a string
#define STRINGIFY( x) #x
#define STR(x) STRINGIFY(x)

static void print_usage(bool error) {
	static const char* usage_str = \
	"Usage: cgraph-cli\n"
	"    -h,--help                       show this help\n"
	"\n"
	" * to compress a hypergraph:\n"
	"   cgraph-cli [options] [input] [output]\n"
	"                       [input]          input file of the hypergraph\n"
	"                       [output]         output file of the compressed graph\n"
	"\n"
	"   optional options:\n"
	"    -f,--format        [format]         format of the RDF graph; keep empty to auto detect the format\n"
	"                                        possible values: \"hyperedge\"\n"
	"       --overwrite                      overwrite if the output file exists\n"
	"    -v,--verbose                        print advanced information\n"
	"\n"
	"   options to influence the resulting size and the runtime to browse the graph (optional):\n"
	"       --max-rank      [rank]           maximum rank of edges, set to 0 to remove limit (default: " STR(DEFAULT_MAX_RANK) ")\n"
	"       --monograms                      enable the replacement of monograms\n"
	"       --factor        [factor]         number of blocks of a bit sequence that are grouped into a superblock (default: " STR(DEFAULT_FACTOR) ")\n"
	"       --no-table                       do not add an extra table to speed up the decompression of the edges for an specific label\n"
#ifdef RRR
    "    --rrr                               use bitsequences based on R. Raman, V. Raman, and S. S. Rao [experimental]\n"
    "                                        --factor can also be applied to this type of bit sequences\n"
#endif
#ifndef RRR
    "    --rrr                               not available. Recompile with -DWITH_RRR=on\n"
#endif
	"\n"
	" * to read a compressed RDF graph:\n"
	"   cgraph-cli [options] [input] [commands...]\n"
	"                       [input]      input file of the compressed RDF graph\n"
	"\n"
	"   optional options:\n"
	"    -f,--format        [format]         default format for the RDF graph at the command `--decompress`\n"
	"                                        possible values: \"turtle\", \"ntriples\", \"nquads\", \"trig\"\n"
	"       --overwrite                      overwrite if the output file exists, used with `--decompress`\n"
	"\n"
	"   commands to read the compressed path:\n"
	"       --decompress    [RDF graph]      decompresses the given compressed RDF graph\n"
    "       --hyperedges    [rank,label]*{,node}\n"
    "                                        determines the edges with given rank. You can specify any number of nodes\n"
    "                                        that will be checked the edge is connected to. The incidence-type is given\n"
    "                                        implicitly. The label must not be set, use ? otherwise. For example:\n"
    "                                        - \"4,2,?,3,?,4\": determines all rank 4 edges with label 2 that are connected\n"
    "                                           to the node 3 with connection-type 2 and node 4 with connection-type 4.\n"
    "                                        - \"2,?,?,5\": determines all rank 2 edges any label that are connected\n"
    "                                           to the node 5 with connection-type 1. In the sense of regular edges, \n"
    "                                           this asks for all incoming edges of node 5.\n"
    "                                        Note that it is not allowed to pass no label and no nodes to this function.\n"
    "                                        Use --decompress in this case.\n"
    "         --exist-query                  Use this flag together with hyperedge to indicate \n"
    "                                        that we look if there is an edge that contains all provided nodes.\n"
    "         --exact-query                  check if there is an edge containing exactly these nodes and no other.\n"
    "         --sort-result                  sort the resulting edges using quicksort.\n"
    "       --query-file                     input file with one line per query. For testing only.\n"
	"       --node-count                     returns the number of nodes in the graph\n"
	"       --edge-labels                    returns the number of different edge labels in the graph\n"
	;

	FILE* os = error ? stderr : stdout;
	fprintf(os, "%s", usage_str);
}

enum opt {
	OPT_HELP = 'h',
	OPT_VERBOSE = 'v',

	OPT_CR_FORMAT = 'f',
	OPT_CR_OVERWRITE = 1000,

	OPT_C_MAX_RANK,
	OPT_C_MONOGRAMS,
	OPT_C_FACTOR,
	OPT_C_NO_TABLE,
#ifdef RRR
	OPT_C_RRR,
#endif

	OPT_R_DECOMPRESS,
	OPT_R_EDGES,
    OPT_R_HYPEREDGES,
    OPT_R_EXIST_QUERY,
    OPT_R_EXACT_QUERY,
    OPT_R_SORT_RESULT,
    OPT_R_QUERY_FILE,
	OPT_R_NODE_COUNT,
	OPT_R_EDGE_LABELS,
};

typedef enum {
	CMD_NONE = 0,
	CMD_DECOMPRESS,
	CMD_EDGES,
    CMD_HYPEREDGES,
    CMD_QUERY_FILE,
	CMD_NODE_COUNT,
	CMD_EDGE_LABELS,
} CGraphCmd;

typedef struct {
	CGraphCmd cmd;
	union {
		char* arg_str;
		uint64_t arg_int;
	};
} CGraphCommand;

typedef struct {
	// -1 = unknown, 0 = compress, 1 = decompress
	int mode;

	bool verbose;
	char* format;
	bool overwrite;

	// options for compression
	CGraphCParams params;

	// options for reading
	int command_count;
	CGraphCommand commands[1024];
} CGraphArgs;

#define check_mode(mode_compress, mode_read, compress_expected) \
do { \
	if(compress_expected) { \
		if(mode_read) { \
			fprintf(stderr, "option '--%s' not allowed when compressing\n", options[longid].name); \
			return -1; \
		} \
		mode_compress = true; \
	} \
	else { \
		if(mode_compress) { \
			fprintf(stderr, "option '--%s' not allowed when reading the compressed graph\n", options[longid].name); \
			return -1; \
		} \
		mode_read = true; \
	} \
} while(0)

#define add_command_str(argd, c) \
do { \
	if((argd)->command_count == (sizeof((argd)->commands) / sizeof(*(argd)->commands))) { \
		fprintf(stderr, "exceeded the maximum number of commands\n"); \
		return -1; \
	} \
	(argd)->commands[(argd)->command_count].cmd = (c); \
	(argd)->commands[(argd)->command_count].arg_str = optarg; \
	(argd)->command_count++; \
} while(0)

#define add_command_int(argd, c) \
do { \
	if((argd)->command_count == (sizeof((argd)->commands) / sizeof(*(argd)->commands))) { \
		fprintf(stderr, "exceeded the maximum number of commands\n"); \
		return -1; \
	} \
	if(parse_optarg_int(&v) < 0) \
		return -1; \
	(argd)->commands[(argd)->command_count].cmd = (c); \
	(argd)->commands[(argd)->command_count].arg_int = v; \
	(argd)->command_count++; \
} while(0)

#define add_command_none(argd, c) \
do { \
	if((argd)->command_count == (sizeof((argd)->commands) / sizeof(*(argd)->commands))) { \
		fprintf(stderr, "exceeded the maximum number of commands\n"); \
		return -1; \
	} \
	(argd)->commands[(argd)->command_count].cmd = (c); \
	(argd)->command_count++; \
} while(0)

static const char* parse_int(const char* s, uint64_t* value) {
	if(!s || *s == '-') // handle negative values because `strtoull` ignores them
		return NULL;

	char* temp;
	uint64_t val = strtoull(s, &temp, 0);
	if(temp == s || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE))
		return NULL;

	*value = val;
	return temp;
}

static inline int parse_optarg_int(uint64_t* value) {
	const char* temp = parse_int(optarg, value);
	if(!temp || *temp != '\0')
		return -1;
	return 0;
}

static int parse_args(int argc, char** argv, CGraphArgs* argd) {
	static struct option options[] = {
		// basic options
		{"help", no_argument, 0, OPT_HELP},
		{"verbose", no_argument, 0, OPT_VERBOSE},

		// options for compression or reading
		{"format",  required_argument, 0, OPT_CR_FORMAT},
		{"overwrite", no_argument, 0, OPT_CR_OVERWRITE},

		// options used for compression
		{"max-rank", required_argument, 0, OPT_C_MAX_RANK},
		{"monograms", no_argument, 0, OPT_C_MONOGRAMS},
		{"factor", required_argument, 0, OPT_C_FACTOR},
		{"no-table", no_argument, 0, OPT_C_NO_TABLE},
#ifdef RRR
		{"rrr", no_argument, 0, OPT_C_RRR},
#endif

		// options used for browsing
		{"decompress", required_argument, 0, OPT_R_DECOMPRESS},
		{"edges", required_argument, 0, OPT_R_EDGES},
        {"hyperedges", required_argument, 0, OPT_R_HYPEREDGES},
        {"exist-query", no_argument, 0, OPT_R_EXIST_QUERY},
        {"exact-query", no_argument, 0, OPT_R_EXACT_QUERY},
        {"sort-result", no_argument, 0, OPT_R_SORT_RESULT},
        {"query-file", required_argument, 0, OPT_R_QUERY_FILE},
		{"node-count", no_argument, 0, OPT_R_NODE_COUNT},
		{"edge-labels", no_argument, 0, OPT_R_EDGE_LABELS},

		{0, 0, 0, 0}
	};

	bool mode_compress = false;
	bool mode_read = false;

	argd->mode = -1;
	argd->verbose = false;
	argd->format = NULL;
	argd->overwrite = false;
	argd->params.max_rank = DEFAULT_MAX_RANK;
	argd->params.monograms = DEFAULT_MONOGRAMS;
	argd->params.factor = DEFAULT_FACTOR;
	argd->params.nt_table = DEFAULT_NT_TABLE;
    argd->params.exist_query = DEFAULT_EXIST_QUERY;
    argd->params.exact_query = DEFAULT_EXACT_QUERY;
    argd->params.sort_result = DEFAULT_SORT_RESULT;
#ifdef RRR
	argd->params.rrr = DEFAULT_RRR;
#endif
	argd->command_count = 0;

	uint64_t v;
	int opt, longid;
	while((opt = getopt_long(argc, argv, "hvf:", options, &longid)) != -1) {
		switch(opt) {
		case OPT_HELP:
			print_usage(false);
			exit(0);
			break;
		case OPT_VERBOSE:
			// TODO: only when compressing a graph, on reading there is no verbose yet.
			argd->verbose = 1;
			break;
		case OPT_CR_FORMAT:
			argd->format = optarg;
			break;
		case OPT_CR_OVERWRITE:
			argd->overwrite = true;
			break;
		case OPT_C_MAX_RANK:
			check_mode(mode_compress, mode_read, true);
			if(parse_optarg_int(&v) < 0) {
				fprintf(stderr, "max-rank: expected integer\n");
				return -1;
			}

			argd->params.max_rank = v;
			break;
		case OPT_C_MONOGRAMS:
			check_mode(mode_compress, mode_read, true);
			argd->params.monograms = true;
			break;
		case OPT_C_FACTOR:
			check_mode(mode_compress, mode_read, true);
			if(parse_optarg_int(&v) < 0) {
				fprintf(stderr, "factor: expected integer\n");
				return -1;
			}

			argd->params.factor = v;
			break;
		case OPT_C_NO_TABLE:
			check_mode(mode_compress, mode_read, true);
			argd->params.nt_table = false;
			break;
#ifdef RRR
		case OPT_C_RRR:
			check_mode(mode_compress, mode_read, true);
			argd->params.rrr = true;
			break;
#endif
		case OPT_R_DECOMPRESS:
			check_mode(mode_compress, mode_read, false);
			add_command_str(argd, CMD_DECOMPRESS);
			break;
		case OPT_R_EDGES:
			check_mode(mode_compress, mode_read, false);
			add_command_str(argd, CMD_EDGES);
			break;
        case OPT_R_HYPEREDGES:
            check_mode(mode_compress, mode_read, false);
            add_command_str(argd, CMD_HYPEREDGES);
            break;
        case OPT_R_EXIST_QUERY:
            check_mode(mode_compress, mode_read, false);
            argd->params.exist_query = true;
            break;
        case OPT_R_EXACT_QUERY:
            check_mode(mode_compress, mode_read, false);
            argd->params.exact_query = true;
            break;
        case OPT_R_SORT_RESULT:
            check_mode(mode_compress, mode_read, false);
            argd->params.sort_result = true;
            break;
        case OPT_R_QUERY_FILE:
            check_mode(mode_compress, mode_read, false);
            add_command_str(argd, CMD_QUERY_FILE);
            break;
		case OPT_R_NODE_COUNT:
			check_mode(mode_compress, mode_read, false);
			add_command_none(argd, CMD_NODE_COUNT);
			break;
		case OPT_R_EDGE_LABELS:
			check_mode(mode_compress, mode_read, false);
			add_command_none(argd, CMD_EDGE_LABELS);
			break;
		case '?':
		case ':':
		default:
			return -1;
		}
	}

	if(mode_compress)
		argd->mode = 0;
	else if(mode_read)
		argd->mode = 1;

	return optind;
}

typedef struct {
	int syntax;
	const char* name;
	const char* extension;
} Syntax;

static const Syntax syntaxes[] = {
    {1, "hyperedge", ".hyperedge"},
	{0, NULL, NULL}
};

static int get_format(const char* format) {
    return syntaxes[0].syntax;
//	for(const Syntax* s = syntaxes; s->name; s++) {
//		if(!strncasecmp(s->name, format, strlen(format))) {
//			return s->syntax;
//		}
//	}
//  return 0;
}
static int guess_format(const char* filename) {
    return syntaxes[0].syntax;
//	const char* ext = strrchr(filename, '.');
//	if(ext) {
//		for(const Syntax* s = syntaxes; s->name; s++) {
//			if(!strncasecmp(s->extension, ext, strlen(ext))) {
//				return s->syntax;
//			}
//		}
//	}
//
//	return 0;
}

#define MAX_LINE_LENGTH (8*LIMIT_MAX_RANK)  // With 8, it gives 1024 for a max_rank of 128.
static int hyperedge_parse(const char* filename, int syntax, CGraphW* g) {
    int res = -1;

    bool err = false;

    FILE* in_fd = fopen((const char*) filename, "r");
    if(!in_fd)
        return res;

    char line[MAX_LINE_LENGTH];
    CGraphNode n[LIMIT_MAX_RANK];
    size_t cn = 0;
    while (fgets(line, sizeof(line), in_fd) && !err) {
        cn = 0;
        // Process each line of the hyperedge file
        // Split the line into individual items using strtok
        char* token = strtok(line, " \t\n"); //Use empty space, tab, and newline as delimiter.
        while (token != NULL) {
            n[cn++] = strtoll(token, NULL, 10); // TODO: Get Endpointer??
            if (cn == LIMIT_MAX_RANK)
                return -1; //Allowed number of parameters are exceeded.
            token = strtok(NULL, " \t\n");
        }
        if (cgraphw_add_edge(g, cn-1, n[0], (n + 1)) < 0) {
            err = true;
        }
    }

    fclose(in_fd);

    if(!err)
        res = 0;

    return res;
}

#define MAX_LINE_LENGTH (8*LIMIT_MAX_RANK)  // With 8, it gives 1024 for a max_rank of 128.
static int cornell_hyperedge_parse(const char* filename, int syntax, CGraphW* g) {
    int res = -1;

    bool err = false;
    char line[MAX_LINE_LENGTH];
    CGraphNode n[LIMIT_MAX_RANK];

    FILE* edge_file = fopen((const char*) filename, "r");

    if(!edge_file)
        return res;

    uint64_t cn = 0;
    while (fgets(line, sizeof(line), edge_file) && !err) {
        cn = 0;
        // Process each line of the hyperedge file
        // Split the line into individual items using strtok
        char* token = strtok(line, " ,\t\n"); //Use empty space, tab, and newline as delimiter.
        while (token != NULL) {
            n[cn++] = strtoll(token, NULL, 0);
            if (cn == LIMIT_MAX_RANK)
                return -1; //Allowed number of parameters are exceeded.
            token = strtok(NULL, " ,\t\n");
        }
        if (cgraphw_add_edge(g, cn,  cn, n) < 0) { // TODO: Label is always cn for this parser, because we need labels depending on the rank.
            err = true;
        }
    }
    fclose(edge_file);

    if(!err)
        res = 0;

    return res;
}

static int do_compress(const char* input, const char* output, const CGraphArgs* argd) {
	if(!argd->overwrite) {
		if(access(output, F_OK) == 0) {
			fprintf(stderr, "Output file \"%s\" already exists.\n", output);
			return -1;
		}
	}

	int syntax;
	if(argd->format)
		syntax = get_format(argd->format);
	else
    {
        syntax = guess_format(input);
        if (argd->verbose && syntax)
        {
            printf("Guessing file format: %s\n", syntaxes[syntax].name);
        }
    }
	if(!syntax)
    {
        syntax = 1;
        if (argd->verbose)
        {
            printf("Guessing file format: %s\n", syntaxes[syntax].name);
        }
    }

	if(argd->verbose) {
		printf("Compression parameters:\n");
		printf("- max-rank: %d\n", argd->params.max_rank);
		printf("- monograms: %s\n", argd->params.monograms ? "true" : "false");
		printf("- factor: %d\n", argd->params.factor);
		printf("- nt-table: %s\n", argd->params.nt_table ? "true" : "false");
#ifdef RRR
		printf("- rrr: %s\n", argd->params.rrr ? "true" : "false");
#endif
	}

	CGraphW* g = cgraphw_init();
	if(!g)
		return -1;

	cgraphw_set_params(g, &argd->params);

	int res = -1;

    if (syntax == syntaxes[0].syntax)
    {
        if(argd->verbose)
            printf("Parsing Cornell Hyperedge file %s\n", input);
        if(cornell_hyperedge_parse(input, syntax, g) < 0) {
            fprintf(stderr, "Failed to read file \"%s\".\n", input);
            goto exit_0;
        }
    }
    else
    if (syntax == syntaxes[1].syntax)
    {
        if(argd->verbose)
            printf("Parsing Hyperedge file %s\n", input);
        if(hyperedge_parse(input, syntax, g) < 0) {
            fprintf(stderr, "Failed to read file \"%s\".\n", input);
            goto exit_0;
        }
    }

	if(argd->verbose)
		printf("Applying repair compression\n");

	if(cgraphw_compress(g) < 0) {
		fprintf(stderr, "failed to compress graph\n");
		goto exit_0;
	}

	if(argd->verbose)
		printf("Writing compressed graph to %s\n", output);

	if(cgraphw_write(g, output, argd->verbose) < 0) {
		fprintf(stderr, "failed to write compressed graph\n");
		goto exit_0;
	}

	res = 0;

exit_0:
	cgraphw_destroy(g);
	return res;
}

static int do_decompress(CGraphR* g, const char* output, const char* format, bool overwrite) {
	int res = -1;

	if(!overwrite) {
		if(access(output, F_OK) == 0) {
			fprintf(stderr, "Output file \"%s\" already exists.\n", output);
			return -1;
		}
	}

	int syntax;
	if(format)
		syntax = get_format(format);
	else
		syntax = guess_format(output);
	if(!syntax)
		syntax = syntaxes[0].syntax;


    FILE* out_fd = fopen((const char*) output, "w+");
    if(!out_fd) {
        fprintf(stderr, "Failed to write to file \"%s\".\n", output);
        goto exit_0;
    }

    if (syntax == syntaxes[0].syntax) // Syntax is hyperedge file
    {
        CGraphNode node;
        CGraphEdgeIterator *it = cgraphr_edges_all(g);
        if (!it) {
            goto exit_0;
        }
        uint64_t number_of_edges = 0;
        CGraphEdge n;
        while (cgraphr_edges_next(it, &n)) {
            for (CGraphRank i = 0; i < n.rank; i++)
            {
                node = n.nodes[i];
                //Write empty space plus txt here;
                if (i > 0)
                {
                    int suc = printf(",");
                    if (suc  == EOF) {
                        cgraphr_edges_finish(it);
                        goto exit_0;
                    }
                }
                int suc = fprintf(out_fd, "%lld", node);
                if (suc  == EOF) {
                    cgraphr_edges_finish(it);
                    goto exit_0;
                }
            }
            if (fprintf(out_fd, "\n") == EOF)
            {
                cgraphr_edges_finish(it);
                goto exit_0;
            }
            number_of_edges++;
        }
        printf("Decompressed %llu edges.\n", number_of_edges);
        res = 0;
    }
exit_0:
    fclose(out_fd);
	return res;
}

// ******* Helper functions *******

typedef struct {
	uint64_t node_src;
	uint64_t node_dst;
	uint64_t label;
} EdgeArg;

int parse_edge_arg(const char* s, EdgeArg* arg) {
	uint64_t node_src, node_dst, label;

	s = parse_int(s, &node_src);
	if(!s)
		return -1;

	switch(*s) {
	case '\0':
		node_dst = -1;
		label = -1;
		goto exit;
	case ',':
		break;
	default:
		return -1;
	}

	if(*(++s) == '?') {
		s++;
		node_dst = -1;
	}
	else {
		s = parse_int(s, &node_dst);
		if(!s)
			return -1;
	}

	switch(*s) {
	case '\0':
		label = -1;
		goto exit;
	case ',':
		break;
	default:
		return -1;
	}

	s = parse_int(s + 1, &label);
	if(!s || *s != '\0')
		return -1;

exit:
	arg->node_src = node_src;
	arg->node_dst = node_dst;
	arg->label = label;
	return 0;
}

typedef struct {
    /* length of nodes*/
    CGraphRank rank;
    /* The nodes of the edge. */
    CGraphNode nodes[LIMIT_MAX_RANK];
} HyperedgeArg;

int parse_hyperedge_arg(const char* s, HyperedgeArg* arg) {
    for (int j=0; j < LIMIT_MAX_RANK; j++) {
        arg->nodes[j] = -1;
    }
    arg->rank = 0;

    s = parse_int(s, (uint64_t *) &arg->nodes[0]);
    if(!s)
        return -1;
    arg->rank++;

    for (int npc = 1; * s == ','; npc++) {
        s = parse_int(s+1, (uint64_t *) &arg->nodes[npc]);
        if (!s)
            return -1;
        arg->rank++;
    }
    return 0;
}

// comparing two nodes
static int cmp_node(const void* e1, const void* e2)  {
	CGraphNode* v1 = (CGraphNode*) e1;
	CGraphNode* v2 = (CGraphNode*) e2;

	int64_t cmpn = *v1 - *v2;
	if(cmpn > 0) return  1;
	if(cmpn < 0) return -1;
	return 0;
}

typedef struct {
	size_t len;
	size_t cap;
	CGraphNode* data;
} NodeList;

// low level list with capacity
void node_append(NodeList* l, CGraphNode n) {
	size_t cap = l->cap;
	size_t len = l->len;

	if(cap == len) {
		cap = !cap ? 8 : (cap + (cap >> 1));
		CGraphNode* data = realloc(l->data, cap * sizeof(*data));
		if(!data)
			exit(1);

		l->cap = cap;
		l->data = data;
	}

	l->data[l->len++] = n;
}

// comparing two edges to sort it
//  1. by label (higher label first)
//  2. by node (higher node first) and
//  3. by rank (higher rank first)
static int cmp_edge(const void* e1, const void* e2)  {

	CGraphEdge* v1 = (CGraphEdge*) e1;
	CGraphEdge* v2 = (CGraphEdge*) e2;

    int64_t cmp = v1->label - v2->label;
	if(cmp > 0) return  1;
	if(cmp < 0) return -1;
    for (int j=0; j < MIN(v1->rank, v2->rank); j++)
    {
        cmp = v1->nodes[j] - v2->nodes[j];
        if(cmp > 0) return  1;
        if(cmp < 0) return -1;
    }
    cmp = v1->rank - v2->rank;
    if(cmp > 0) return  1;
    if(cmp < 0) return -1;
	return 0;
}

typedef struct {
	size_t len;
	size_t cap;
	CGraphEdge* data;
} EdgeList;

// low level list with capacity
void edge_append(EdgeList* l, CGraphEdge* e) {
	size_t cap = l->cap;
	size_t len = l->len;

	if(cap == len) {
		cap = !cap ? 8 : (cap + (cap >> 1));
		CGraphEdge* data = realloc(l->data, cap * sizeof(*data));
		if(!data)
			exit(1);

		l->cap = cap;
		l->data = data;
	}

	l->data[l->len++] = *e;
}

bool do_search(CGraphR* g, CGraphRank rank, CGraphNode* nodes, bool exist_query, bool exact_query, bool sort_result, EdgeList* result)
{
    if (exist_query) {
        return cgraphr_edge_exists(g, rank, nodes, exact_query, true);
    }
    CGraphEdgeIterator* it;
    it = cgraphr_edges(g, rank, nodes, exact_query, true);


    if(!it)
        return false;

    CGraphEdge n;
    while(cgraphr_edges_next(it, &n))
        edge_append(result, &n);

    // sort the edges
    if (sort_result)
    {
        qsort(result->data, result->len, sizeof(CGraphEdge), cmp_edge);
    }
    return result->len > 0;
}

void perform_search(CGraphR* g, CGraphRank rank, CGraphNode* nodes, bool exist_query, bool exact_query, bool sort_result, bool verbose)
{
    EdgeList ls = {0};
    bool has_result = do_search(g, rank, nodes, exist_query, exact_query, sort_result, &ls);

    if (exist_query)
    {
        printf("%d\n", has_result ? 1 : 0);
    }
    else
    {
        printf("Found %zu results\n", ls.len);
        if (verbose)
        {
            for(size_t i = 0; i < ls.len; i++) {
                for (CGraphRank j = 0; j < ls.data[i].rank; j++) {
                    printf(",\t%" PRId64, ls.data[i].nodes[j]);
                }
            }
        }


        for (int j = 0; j < ls.len; j++)
        {
            free(ls.data[j].nodes);
        }
        if(ls.data)
            free(ls.data);

    }
}

int perform_query_file(CGraphR* g, const char* query_file, bool exist_query, bool exact_query, bool sort_result, bool verbose)
{
    FILE* in_fd = fopen((const char*) query_file, "r");
    if(!in_fd)
        return -1;

    char line[MAX_LINE_LENGTH];
    HyperedgeArg arg;
    int cn = 0;
    while (fgets(line, sizeof(line), in_fd)) {
        printf("Query %d: %s", cn, line);
        if (parse_hyperedge_arg(line, &arg) < 0)
        {
            fprintf(stderr, "Parsing error of file.");
            return -1;
        }
        perform_search(g, arg.rank, arg.nodes, exist_query, exact_query, sort_result, verbose);
        cn++;
    }
    fclose(in_fd);
    return 0;
}

static int do_read(const char* input, const CGraphArgs* argd) {
	CGraphR* g = cgraphr_init(input);
	if(!g) {
		fprintf(stderr, "failed to read compressed graph %s\n", input);
		return -1;
	}

	if(argd->command_count == 0) {
		fprintf(stderr, "no commands given\n");
		return -1;
	}

	int res = -1;

	for(int i = 0; i < argd->command_count; i++) {
		const CGraphCommand* cmd = &argd->commands[i];

		switch(cmd->cmd) {
		case CMD_DECOMPRESS:
			if(do_decompress(g, cmd->arg_str, argd->format, argd->overwrite) < 0)
				goto exit; // terminate the reading of the graph

			res = 0;
			break;
		case CMD_EDGES:
            // fallthrough
//        {
//			EdgeArg arg;
//			if(parse_edge_arg(cmd->arg_str, &arg) < 0) {
//				fprintf(stderr, "failed to parse edge argument \"%s\"\n", cmd->arg_str);
//				break;
//			}
//
//			if(arg.label != CGRAPH_LABELS_ALL && arg.node_dst != -1) {
//				bool exists = cgraphr_edge_exists(g, arg.node_src, arg.node_dst, arg.label);
//				printf("%d\n", exists ? 1 : 0);
//				res = 0;
//				break;
//			}
//
//			CGraphEdgeIterator* it = (arg.node_dst == -1) ?
//				cgraphr_edges(g, arg.node_src, arg.label) :
//				cgraphr_edges_connecting(g, arg.node_src, arg.node_dst);
//			if(!it)
//				break;
//
//			CGraphEdge n;
//			EdgeList ls = {0}; // list is empty
//			while(cgraphr_edges_next(it, &n))
//				edge_append(&ls, &n);
//
//			// sort the edges
//			qsort(ls.data, ls.len, sizeof(CGraphEdge), cmp_edge);
//
//			for(size_t i = 0; i < ls.len; i++)
//				printf("%" PRId64 "\t%" PRId64 "\t%" PRId64 "\n", ls.data[i].node1, ls.data[i].node2, ls.data[i].label);
//
//			if(ls.data)
//				free(ls.data);
//
//			res = 0;
//			break;
//		}
        case CMD_HYPEREDGES: {
            HyperedgeArg arg;
            if(parse_hyperedge_arg(cmd->arg_str, &arg) < 0) {
                fprintf(stderr, "failed to parse edge argument \"%s\"\n", cmd->arg_str);
                break;
            }
            perform_search(g, arg.rank, arg.nodes, argd->params.exist_query, argd->params.exact_query, argd->params.sort_result, argd->verbose);
            res = 0;
            break;
        }
        case CMD_QUERY_FILE: {
            if(access(cmd->arg_str, F_OK) != 0) {
                fprintf(stderr, "query file %s does not exists.", cmd->arg_str);
                break;
            }
            perform_query_file(g, cmd->arg_str, argd->params.exist_query, argd->params.exact_query, argd->params.sort_result, argd->verbose);
            res = 0;
            break;
        }
		case CMD_NODE_COUNT:
			printf("%zu\n", cgraphr_node_count(g));
			res = 0;
			break;
		case CMD_EDGE_LABELS:
			printf("%zu\n", cgraphr_edge_label_count(g));
			res = 0;
			break;
		case CMD_NONE:
			goto exit;
		}	
	}

exit:
	cgraphr_destroy(g);
	return res;
}

int main(int argc, char** argv) {
	if(argc <= 1) {
		print_usage(true);
		return EXIT_FAILURE;
	}

	CGraphArgs argd;
	int arg_indx = parse_args(argc, argv, &argd); // ignore first program arg
	if(arg_indx < 0)
		return EXIT_FAILURE;

	char** cmd_argv = argv + arg_indx;
	int cmd_argc = argc - arg_indx;

	if(argd.mode == -1) // unknown mode, determine by the number of arguments
		argd.mode = cmd_argc == 2 ? 0 : 1;
	if(argd.mode == 0) {
		if(cmd_argc != 2) {
			fprintf(stderr, "expected 2 parameters when compressing RDF graphs\n");
			return EXIT_FAILURE;
		}

		if(do_compress(cmd_argv[0], cmd_argv[1], &argd) < 0)
			return EXIT_FAILURE;
	}
	else {
		if(cmd_argc != 1) {
			fprintf(stderr, "expected 1 parameter when reading compressed RDF graphs\n");
			return EXIT_FAILURE;
		}

		if(do_read(cmd_argv[0], &argd) < 0)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
