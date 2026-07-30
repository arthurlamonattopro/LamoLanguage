<div align="center">

# ⚡ Lamo Language

### A modern programming language that compiles to C.

**Simple like a scripting language. Fast like a compiled one.**

[![License](https://img.shields.io/github/license/arthurlamonattopro/LamoLanguage?style=for-the-badge)](LICENSE)
[![Stars](https://img.shields.io/github/stars/arthurlamonattopro/LamoLanguage?style=for-the-badge)]()
[![Issues](https://img.shields.io/github/issues/arthurlamonattopro/LamoLanguage?style=for-the-badge)]()

</div>

---

## 🚀 About

Lamo is an experimental programming language focused on being easy to learn while generating native executables.

Instead of executing code through a virtual machine, **Lamo transpiles your program to C**, then uses **GCC** to produce highly optimized native binaries.

> **Write simple code. Get native performance.**

---

## ✨ Features

- ⚡ Compiles to native executables
- 🧠 Clean and readable syntax
- 📦 Built-in package manager
- 📁 Module system
- 🏗 Structs & methods
- 📚 Enums and match expressions
- 📋 Arrays
- 🌐 Native HTTP server
- 🖥 Native GUI support
- 🎨 Colorized compiler diagnostics
- 🧪 Built-in testing
- 🛠 Code formatter
- 🔍 Semantic analysis
- 🌳 AST generation

---

## Hello World

```lamo
fn main() {
    print("Hello, World!")
}
```

Run it:

```bash
lamo run hello.lamo
```

---

# Example

```lamo
struct Player {
    name: string,
    hp: int
}

impl Player {
    fn damage(amount: int) {
        self.hp -= amount
    }
}

let hero = Player {
    name: "Arthur",
    hp: 100
}

hero.damage(25)

print(hero.hp)
```

---

# Installation

Clone the repository

```bash
git clone https://github.com/arthurlamonattopro/LamoLanguage
cd LamoLanguage
```

Build

```bash
make
```

Run

```bash
./lamo run examples/test.lamo
```

---

# Project Status

Current implementation includes:

- ✅ Lexer
- ✅ Parser
- ✅ AST
- ✅ Semantic Analyzer
- ✅ C Backend
- ✅ Modules
- ✅ Structs
- ✅ Methods
- ✅ Arrays
- ✅ Enums
- ✅ Match
- ✅ Package Manager
- ✅ REPL
- ✅ Formatter
- ✅ Tests

---

# Roadmap

- [ ] Generics
- [ ] Traits
- [ ] Better formatter
- [ ] Language Server (LSP)
- [ ] VSCode extension
- [ ] Official documentation website

---

# Philosophy

Lamo follows one simple idea:

> **As simple as an interpreted language, as fast as a compiled one.**

Instead of creating a complex language with hundreds of features, Lamo aims to remain small, readable and productive.

---

# Documentation

The full documentation can be found in the **docs/** directory.

- Language
- Standard Library
- Memory Model
- Package Manager

---

# Contributing

Contributions are welcome!

If you'd like to improve the compiler, documentation or standard library, feel free to open a Pull Request.

---

# License

This project is licensed under the MIT License.
