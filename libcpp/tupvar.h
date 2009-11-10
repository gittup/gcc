/* This is used for the kernel-style CONFIG_ macros. The variable string is
 * pulled from the dictionary, and then made into either a CPP_STRING or
 * CPP_NUMBER macro depending on the string type.
 *
 * Eg: FOO=test becomes "test" (CPP_STRING)
 *     FOO=3 becomes 3 (CPP_NUMBER)
 *     FOO=0xa becomes 0xa (CPP_NUMBER)
 *
 * The strings "y" and "n" also have special meaning:
 *     FOO=y becomes 1
 *     FOO=n becomes 0, or is left undefined depending on 'n_is_zero'
 */
void tup_set_macro(cpp_reader *pfile, const char *var, int varlen,
		   cpp_hashnode *node, int n_is_zero);

/* This is used for busybox-style ENABLE_ macros. They are set to either 1
 * (CPP_NUMBER) or 0 (CPP_NUMBER)
 *
 * Eg: FOO=y becomes 1
 *     anything else becomes 0
 */
void tup_enable_macro(cpp_reader *pfile, const char *var, int varlen,
		      cpp_hashnode *node);

/* This is for busybox-style IF_ and IF_NOT_ macros. They are similar to the
 * enable macros, except they are function-style macros that either reproduce
 * the input, or remove it entirely.
 *
 * Eg: FOO=y is equivalent to #define IF_FOO(...) __VA_ARGS__
 *     anything else is equivalent to #define IF_FOO(...)
 *
 * The IF_NOT_ case just inverts the logic.
 */
int tup_set_if(cpp_reader *pfile, const char *var, int varlen,
	       cpp_hashnode *node, int invert);
