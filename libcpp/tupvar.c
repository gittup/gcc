#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "tup_client.h"
#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"
#include "tupvar.h"

void tup_set_macro(cpp_reader *pfile, const char *var, int varlen,
		   cpp_hashnode *node, int n_is_zero)
{
	int x;
	int len;
	const char *value;
	int hex = 0;

	value = tup_config_var(var, varlen);
	/* 'n' values are treated as undefined for CONFIG_ variables */
	if(value && (n_is_zero || strcmp(value, "n") != 0)) {
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
		} else if(strcmp(value, "n") == 0) {
			value = "0";
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

	value = tup_config_var(var, varlen);
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

	value = tup_config_var(var, varlen);
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
