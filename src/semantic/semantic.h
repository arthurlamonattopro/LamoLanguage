#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

/* Type of the source-lookup callback used by semantic_analyze to print
 * source snippets under error messages. Given a file_path, returns the
 * full source text of that file (NULL if not known). The returned
 * pointer must remain valid for the duration of semantic_analyze(). */
typedef const char* (*LamoSourceLookupFn)(const char* file_path, void* user_data);

/* Basic entry point: runs the semantic pass with no source-lookup
 * callback. Error messages will be emitted without a source snippet. */
int semantic_analyze(ASTProgram* program, const char* file_path);

/* Sprint 3 entry point: same as semantic_analyze, but registers a
 * source-lookup callback so that error messages include the offending
 * source line + a caret pointing at the column. */
int semantic_analyze_with_source_lookup(ASTProgram* program, const char* file_path,
                                        LamoSourceLookupFn lookup, void* user_data);

#endif
