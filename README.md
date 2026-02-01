# Lamo Language

## Visão Geral

**Lamo** é uma linguagem de programação experimental, desenvolvida do zero, com inspiração direta em **C**, porém adotando uma sintaxe mais moderna e menos verbosa.  
O projeto tem como objetivo estudar e implementar os principais componentes de uma linguagem de programação, incluindo análise léxica, sintática e semântica, mantendo controle explícito sobre memória e execução.

A linguagem foi projetada para ser simples, previsível e extensível, servindo tanto como ferramenta educacional quanto como base para experimentação em design de linguagens.

---

## Filosofia de Design

- Sintaxe imperativa inspirada em C  
- Estruturas de controle explícitas  
- Blocos delimitados por `{ }`  
- Sem dependência de runtime complexo  
- Implementação em C puro  
- Prioridade em clareza e controle, não em abstrações ocultas  

---

## Características da Linguagem

### Estrutura Geral

- Execução sequencial  
- Escopo baseado em blocos  
- Funções definidas com `fn`  
- Declaração de variáveis com `let`  
- Tipagem implícita (fase atual do projeto)  
- Entrada única de execução (script-style)  

---

## Sintaxe Básica

### Declaração de Variáveis

```lamo
let x = 10;
let msg = "Olá, mundo";
```

### Funções

```lamo
fn soma(a, b) {
    return a + b;
}
```

### Condicionais

```lamo
if (x > 10) {
    print("Maior que 10");
} else {
    print("Menor ou igual a 10");
}
```

### Laços de Repetição

#### While

```lamo
while (x < 5) {
    x += 1;
}
```

#### For

```lamo
for (let i = 0; i < 10; i++) {
    print(i);
}
```

---

## Comentários

```lamo
// Comentário de linha

/*
   Comentário de bloco
*/
```

---

## Componentes da Linguagem

1. Lexer (Analisador Léxico)  
2. Parser (Analisador Sintático)  
3. AST (Árvore Sintática Abstrata)  
4. Analisador Semântico  
5. Backend (interpretação ou geração de código)  

Atualmente, o projeto possui o **lexer totalmente funcional**.

---

## Tokens Suportados

### Palavras-chave

```
let, fn, return, if, else, while, for, print, true, false
```

### Literais e Identificadores

```
IDENTIFIER
INT
STRING
```

### Operadores

```
=  +  -  *  /  %
== != < > <= >=
&& || !
+= -= ++ --
```

### Delimitadores

```
( ) { } [ ]
, ; :
```

### Especiais

```
EOF
UNKNOWN
```

---

## Compatibilidade

- Dependência apenas da biblioteca padrão C  
- Implementação própria de `strndup` para portabilidade  
- Compatível com GCC e Clang  

---

## Estado do Projeto

- [x] Sintaxe base definida  
- [x] Lexer implementado  
- [X] Parser  
- [ ] AST  
- [ ] Análise semântica  
- [ ] Interpretador / Transpilador  

---

## Licença

Projeto experimental e educacional.  
Uso livre para estudo e modificação.
