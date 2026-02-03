#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer_v2.h"
#include "parser_v2.h"
#include "ast.h"

#define VERSION "2.0"

void print_usage(const char* prog);
char* read_file(const char* path);
void generate_c_code(ASTNode* node, FILE* out);

void print_usage(const char* prog) {
    printf("Lamo v%s - Linguagem de Programação\n\n", VERSION);
    printf("Uso: %s <arquivo.lamo> [opções]\n\n", prog);
}

char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    return content;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    char* input_file = argv[1];
    char* source = read_file(input_file);
    if (!source) return 1;
    
    printf("Compilando %s...\n", input_file);
    
    Lexer* lexer = lexer_init(source);
    Parser* parser = parser_init(lexer);
    
    printf("Construindo AST...\n");
    ASTProgram* program_ast = parse_program_v2(parser);
    printf("[OK] AST construída em %p\n", (void*)program_ast);

    FILE* out = fopen("lamo_exec.c", "w");
    if (!out) return 1;
    
    printf("Gerando código C...\n");
    generate_c_code((ASTNode*)program_ast, out);
    fclose(out);
    
    printf("[OK] Código C gerado: lamo_exec.c\n");
    
    system("gcc -Wall -o lamo_exec lamo_exec.c");
    printf("\n--- Executando ---\n");
    #ifdef _WIN32
    system("lamo_exec.exe");
    #else
        system("./lamo_exec");
    #endif
    
    return 0;
}
