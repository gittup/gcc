#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "access_event.h"
#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"
#include "tupvar.h"

struct vardict {
	unsigned int len;
	unsigned int num_entries;
	unsigned int *offsets;
	const char *entries;
	void *map;
};
static void tup_var_init(void);
static int init_vardict(int fd, struct vardict *vars);
static const char *vardict_search(struct vardict *vars, const char *key,
				  int keylen);
static int tup_send_event(const char *var, int len);

static struct vardict tup_vars;
static int tup_sd;

void tup_set_macro(cpp_reader *pfile, const char *var, int varlen,
		   cpp_hashnode *node)
{
	int x;
	int len;
	const char *value;
	int hex = 0;

	tup_var_init();

	value = vardict_search(&tup_vars, var, varlen);
	/* 'n' values are treated as undefined for CONFIG_ variables */
	if(value && strcmp(value, "n") != 0) {
		cpp_macro *macro;

		macro = (cpp_macro*) pfile->hash_table->alloc_subobject(sizeof *macro);
		macro->line = pfile->directive_line;
		macro->params = 0;
		macro->paramc = 0;
		macro->variadic = 0;
		macro->used = !CPP_OPTION (pfile, warn_unused_macros);
		macro->count = 1;
		macro->traditional = 0;
		macro->fun_like = 0;
		/* To suppress some diagnostics.  */
		macro->syshdr = pfile->buffer && pfile->buffer->sysp != 0;

		macro->exp.tokens = pfile->hash_table->alloc_subobject(sizeof(cpp_token));

		if(strcmp(value, "y") == 0) {
			value = "1";
			len = 1;
		}
		len = strlen(value);
		x = 0;
		/* Allow hex numbers */
		if(len > 2 && value[0] == '0' && value[1] == 'x') {
			hex = 1;
			x = 2;
		}
		for(; x<len; x++) {
			unsigned char *str;
			if(value[x] >= '0' && value[x] <= '9')
				continue;
			if(hex && value[x] >= 'a' && value[x] <= 'f')
				continue;
			if(hex && value[x] >= 'A' && value[x] <= 'F')
				continue;
			/* String values get wrapped in quotes */
			macro->exp.tokens[0].type = CPP_STRING;
			macro->exp.tokens[0].val.str.len = len + 2;
			str = (unsigned char*)xmalloc(len + 3);
			str[0] = '"';
			memcpy(str+1, value, len);
			str[len+1] = '"';
			str[len+2] = 0;
			macro->exp.tokens[0].val.str.text = str;
			goto macro_set;
		}
		macro->exp.tokens[0].type = CPP_NUMBER;
		macro->exp.tokens[0].val.str.len = len;
		macro->exp.tokens[0].val.str.text = (unsigned char*)xstrdup(value);
macro_set:
		macro->exp.tokens[0].flags = 0;
		macro->exp.tokens[0].src_loc = pfile->directive_line;

		node->type = NT_MACRO;
		node->value.macro = macro;
	}
}

void tup_enable_macro(cpp_reader *pfile, const char *var, int varlen,
		      cpp_hashnode *node)
{
	const char *value;
	cpp_macro *macro;

	tup_var_init();

	value = vardict_search(&tup_vars, var, varlen);
	if(value && strcmp(value, "y") == 0) {
		value = "1";
	} else {
		value = "0";
	}

	macro = (cpp_macro*) pfile->hash_table->alloc_subobject(sizeof *macro);
	macro->line = pfile->directive_line;
	macro->params = 0;
	macro->paramc = 0;
	macro->variadic = 0;
	macro->used = !CPP_OPTION (pfile, warn_unused_macros);
	macro->count = 1;
	macro->traditional = 0;
	macro->fun_like = 0;
	/* To suppress some diagnostics.  */
	macro->syshdr = pfile->buffer && pfile->buffer->sysp != 0;

	macro->exp.tokens = pfile->hash_table->alloc_subobject(sizeof(cpp_token));

	macro->exp.tokens[0].type = CPP_NUMBER;
	macro->exp.tokens[0].val.str.len = 1;
	macro->exp.tokens[0].val.str.text = (unsigned char*)xstrdup(value);
	macro->exp.tokens[0].flags = 0;
	macro->exp.tokens[0].src_loc = pfile->directive_line;

	node->type = NT_MACRO;
	node->value.macro = macro;
}

int tup_set_if(cpp_reader *pfile, const char *var, int varlen,
	       cpp_hashnode *node, int invert)
{
	const char *value;
	cpp_macro *macro;

	tup_var_init();

	macro = (cpp_macro*) pfile->hash_table->alloc_subobject(sizeof *macro);
	macro->line = pfile->directive_line;
	macro->params = (cpp_hashnode**)pfile->hash_table->alloc_subobject(sizeof (cpp_hashnode*));
	macro->params[0] = pfile->spec_nodes.n__VA_ARGS__;
	macro->paramc = 1;
	macro->variadic = 1;
	macro->used = !CPP_OPTION (pfile, warn_unused_macros);
	macro->traditional = 0;
	macro->fun_like = 1;
	/* To suppress some diagnostics.  */
	macro->syshdr = pfile->buffer && pfile->buffer->sysp != 0;

	value = vardict_search(&tup_vars, var, varlen);
	if((value && strcmp(value, "y") == 0) ^ invert) {
		macro->count = 1;
		macro->exp.tokens = pfile->hash_table->alloc_subobject(sizeof(cpp_token));
		macro->exp.tokens[0].type = CPP_MACRO_ARG;
		macro->exp.tokens[0].flags = 0;
		macro->exp.tokens[0].src_loc = pfile->directive_line;
		macro->exp.tokens[0].val.arg_no = 1;
	} else {
		macro->count = 0;
		macro->exp.tokens = NULL;
	}

	node->type = NT_MACRO;
	node->value.macro = macro;
	return 0;
}

static void tup_var_init(void)
{
	static int inited = 0;
	char *path;
	int vardict_fd;

	if(inited)
		return;
	path = getenv(TUP_SERVER_NAME);
	if(!path) {
		fprintf(stderr, "gcc/tup error: TUP_SERVER_NAME not defined in libcpp/tupvar.c - abort\n");
		abort();
	}
	tup_sd = strtol(path, NULL, 0);
	if(tup_sd <= 0) {
		fprintf(stderr, "gcc/tup error: Unable to find tup_sd - abort\n");
		abort();
	}

	path = getenv(TUP_VARDICT_NAME);
	if(!path) {
		fprintf(stderr, "gcc/tup error: Couldn't find path for '%s'\n",
			TUP_VARDICT_NAME);
		abort();
	}
	vardict_fd = strtol(path, NULL, 0);
	if(vardict_fd <= 0) {
		fprintf(stderr, "gcc/tup error: vardict_fd <= 0\n");
		abort();
	}
	if(init_vardict(vardict_fd, &tup_vars) < 0) {
		fprintf(stderr, "gcc/tup error: init_vardict() failed\n");
		abort();
	}

	inited = 1;
}

static int init_vardict(int fd, struct vardict *vars)
{
	struct stat buf;
	unsigned int expected = 0;

	if(fstat(fd, &buf) < 0) {
		perror("fstat");
		return -1;
	}
	vars->len = buf.st_size;
	expected += sizeof(unsigned int);
	if(vars->len < expected) {
		fprintf(stderr, "Error: var-tree should be at least sizeof(unsigned int) bytes, but got %i bytes\n", vars->len);
		return -1;
	}
	vars->map = mmap(NULL, vars->len, PROT_READ, MAP_PRIVATE, fd, 0);
	if(vars->map == MAP_FAILED) {
		perror("mmap");
		return -1;
	}

	vars->num_entries = *(unsigned int*)vars->map;
	vars->offsets = (unsigned int*)((char*)vars->map + expected);
	expected += sizeof(unsigned int) * vars->num_entries;
	vars->entries = (const char*)vars->map + expected;
	if(vars->len < expected) {
		fprintf(stderr, "Error: var-tree should have at least %i bytes to accommodate the index, but got %i bytes\n", expected, vars->len);
		return -1;
	}

	return 0;
}

static const char *vardict_search(struct vardict *vars, const char *key,
				  int keylen)
{
	int left = -1;
	int right = vars->num_entries;
	int cur;
	const char *p;
	const char *k;
	int bytesleft;

	while(1) {
		cur = (right - left) >> 1;
		if(cur <= 0)
			break;
		cur += left;
		if(cur >= (signed)vars->num_entries)
			break;

		if(vars->offsets[cur] >= vars->len) {
			fprintf(stderr, "Error: Offset for element %i is out of bounds.\n", cur);
			break;
		}
		p = vars->entries + vars->offsets[cur];
		k = key;
		bytesleft = keylen;
		while(bytesleft > 0) {
			/* Treat '=' as if p ended */
			if(*p == '=') {
				left = cur;
				goto out_next;
			}
			if(*p < *k) {
				left = cur;
				goto out_next;
			} else if(*p > *k) {
				right = cur;
				goto out_next;
			}
			p++;
			k++;
			bytesleft--;
		}

		if(*p != '=') {
			right = cur;
			goto out_next;
		}
		tup_send_event(vars->entries + vars->offsets[cur],
			       p - (vars->entries + vars->offsets[cur]));
		if(strcmp(p+1, "~UNSET~") == 0)
			return NULL;
		return p+1;
out_next:
		;
	}
	return NULL;
}

static int tup_send_event(const char *var, int len)
{
	struct access_event *event;
	static char msgbuf[sizeof(*event) + PATH_MAX];
	int rc = -1;

	if(!tup_sd)
		return -1;

	event = (struct access_event*)msgbuf;
	event->at = ACCESS_VAR;
	event->len = len + 1;
	event->len2 = 0;
	if(event->len >= PATH_MAX) {
		fprintf(stderr, "tup.ldpreload error: Path too long (%i bytes)\n", event->len);
		return -1;
	}
	memcpy(msgbuf + sizeof(*event), var, len);
	msgbuf[sizeof(*event) + len] = 0;
	rc = send(tup_sd, msgbuf, sizeof(*event) + event->len, 0);
	if(rc < 0) {
		perror("tup send");
		return -1;
	}
	return 0;
}
