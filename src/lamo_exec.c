// Código gerado por Lamo v2 (via AST)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int fatorial(int n);

int fatorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fatorial(n - 1);
}

int main() {
    int x = 10;
    int y = 20;
    int z = x + y;
    printf("%s\n", "Resultado da soma:");
    printf("%d\n", z);
    printf("%s\n", "Fatorial de 5:");
    printf("%d\n", fatorial(5));
    int i = 0;
    printf("%s\n", "Contagem ate 5:");
    while (i < 5) {
        i = i + 1;
        printf("%d\n", i);
    }
    printf("%s\n", "Loop for:");
    for (int j = 0; j < 3; j = j + 1) {
        printf("%d\n", j);
    }
    return 0;
}
