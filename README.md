# Lexer V2 - Analisador Léxico em C

Este repositório contém uma implementação robusta de um **Lexer** (Analisador Léxico) desenvolvido em C. Ele é o componente inicial de um compilador ou interpretador, responsável por converter o código-fonte bruto em uma sequência de tokens significativos.

## 🚀 Funcionalidades

* **Palavras-chave (Keywords):** Reconhece `let`, `fn`, `return`, `if`, `else`, `while`, `for`, `print`, `true` e `false`.
* **Operadores Complexos:** Suporte a operadores de dois caracteres como `==`, `!=`, `<=`, `>=`, `&&`, `||`, `++`, `--`, `+=` e `-=`.
* **Gestão de Comentários:** Ignora automaticamente comentários de linha única (`//`) e blocos de comentários (`/* ... */`).
* **Rastreamento de Posição:** Armazena linha e coluna de cada token para mensagens de erro precisas.
* **Literais:** Suporte para números inteiros, identificadores (nomes de variáveis/funções) e strings (com suporte a caracteres de escape).

## 🛠️ Arquitetura do Lexer

O Lexer funciona como uma máquina de estados que consome caracteres um a um, utilizando as funções `peek()` para olhar o próximo caractere sem consumi-lo e `advance()` para mover o ponteiro de leitura.



## 📂 Estrutura de Arquivos

* `lexer_v2.c`: Implementação da lógica de análise.
* `lexer_v2.h`: Definições de tipos (`TokenType`), structs (`Token`, `Lexer`) e protótipos de funções.

## 💻 Exemplo de Uso

Para integrar o lexer ao seu projeto, siga o exemplo abaixo:

```c
#include <stdio.h>
#include "lexer_v2.h"

int main() {
    char source[] = "let x = 10; /* exemplo */ print(x);";
    Lexer* lexer = lexer_init(source);
    Token token;

    while ((token = lexer_next_token(lexer)).type != TOKEN_EOF) {
        printf("Token: %-12s | Valor: [%s] | L: %d, C: %d\n", 
               token_type_name(token.type), 
               token.value, 
               token.line, 
               token.column);
        token_free(token);
    }

    lexer_free(lexer);
    return 0;
}
