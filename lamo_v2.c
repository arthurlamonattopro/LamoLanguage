/*
 * Lamo v2 - Compilador/Transpilador para C
 * Uma linguagem de programação simples que compila para C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer_v2.c"
#include "parser_v2.c"

#define VERSION "2.0"

void print_usage(const char* prog) {
    printf("Lamo v%s - Linguagem de Programação\n\n", VERSION);
    printf("Uso: %s <arquivo.lamo> [opções]\n\n", prog);
    printf("Opções:\n");
    printf("  -o <nome>   Nome do executável (padrão: lamo_exec)\n");
    printf("  -c          Apenas gerar código C (não compilar)\n");
    printf("  -r          Executar após compilar\n");
    printf("  -h          Mostrar esta ajuda\n");
}

char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Erro: Não foi possível abrir '%s'\n", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
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
    
    // Parse argumentos
    char* input_file = NULL;
    char* output_name = "lamo_exec";
    int only_c = 0;
    int run_after = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_name = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            only_c = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            run_after = 1;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        }
    }
    
    if (!input_file) {
        fprintf(stderr, "Erro: Nenhum arquivo de entrada especificado\n");
        return 1;
    }
    
    // Ler arquivo fonte
    char* source = read_file(input_file);
    if (!source) return 1;
    
    printf("Compilando %s...\n", input_file);
    
    // Criar lexer e parser
    Lexer* lexer = lexer_init(source);
    Parser* parser = parser_init(lexer);
    
    // Gerar código C
    char c_filename[256];
    snprintf(c_filename, sizeof(c_filename), "%s.c", output_name);
    
    FILE* out = fopen(c_filename, "w");
    if (!out) {
        fprintf(stderr, "Erro: Não foi possível criar '%s'\n", c_filename);
        free(source);
        lexer_free(lexer);
        parser_free(parser);
        return 1;
    }
    
    parse_program_v2(parser, out);
    fclose(out);
    
    printf("[OK] Codigo C gerado: %s\n", c_filename);
    
    // Compilar
    if (!only_c) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "gcc -Wall -o %s %s", output_name, c_filename);
        
        int result = system(cmd);
        if (result != 0) {
            fprintf(stderr, "Erro na compilação do código C\n");
            free(source);
            lexer_free(lexer);
            parser_free(parser);
            return 1;
        }
        
        #ifdef _WIN32
        printf("[OK] Executavel criado: %s.exe\n", output_name);
        #else
        printf("[OK] Executavel criado: ./%s\n", output_name);
        #endif
        
        // Executar se solicitado
        if (run_after) {
            printf("\n--- Executando ---\n");
            char run_cmd[256];
            #ifdef _WIN32
            snprintf(run_cmd, sizeof(run_cmd), "%s.exe", output_name);
            #else
            snprintf(run_cmd, sizeof(run_cmd), "./%s", output_name);
            #endif
            system(run_cmd);
        }
    }
    
    // Limpeza
    free(source);
    lexer_free(lexer);
    parser_free(parser);
    
    return 0;
}
