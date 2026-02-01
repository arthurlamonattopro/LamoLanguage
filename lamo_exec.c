// Código gerado por Lamo v2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

    int fatorial(int n) {
        if (n <= 1) {
            return 1;
        }
        return n * fatorial(n - 1);
    }

    int eh_par(int x) {
        return x % 2 == 0;
    }

int main() {
    const char* msg = "Bem-vindo ao Lamo v2!";
    printf("%s\n", msg);
    printf("%s\n", "Contagem de 1 a 5:");
    for (int i = 1; i <= 5; i++) {
        printf("%d\n", i);
    }
    printf("%s\n", "Fatorial de 5:");
    int resultado = fatorial(5);
    printf("%d\n", resultado);
    int a = 10;
    int b = 20;
    if (a < b && eh_par(a)) {
        printf("%s\n", "a é menor que b E é par");
    }
    int contador = 0;
    while (contador < 3) {
        printf("%d\n", contador);
        contador += 1;
    }
    printf("%s\n", "Fim!");
    return 0;
}
