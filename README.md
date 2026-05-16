# Aluno
```glsl
// student.vert
uniform struct Student {
    string name  = "Maurício Pereira da Costa";
    string curso = "Ciência da Computação";
    string inst  = "Unisinos - POA";
} aluno;
```

# Computação Gráfica - Híbrido

Repositório de exemplos de códigos em C++ utilizando OpenGL moderna (3.3+) criado para a Atividade Acadêmica Computação Gráfica do curso de graduação em Ciência da Computação - modalidade híbrida - da Unisinos. Ele é estruturado para facilitar a organização dos arquivos e a compilação dos projetos utilizando CMake.

## 📂 Estrutura do Repositório

```plaintext
📂 ComputacaoGrafica-main/
├── 📂 include/glad/          # Cabeçalhos da GLAD (OpenGL Loader) + KHR/
├── 📂 common/                # Código reutilizável (glad.c)
├── 📂 src/
│   ├── 📂 exemplos/          # Material de apoio da disciplina
│   │   ├── TriangleTex.cpp
│   │   └── SpherePhong.cpp
│   └── 📂 desafios/          # Entregas do aluno
│       └── M2_CubosInterativos.cpp
├── 📂 entregas/              # Prints e descrição de cada desafio
│   ├── 📂 M1/                # README.md + print.png
│   └── 📂 M2/                # README.md + print.png
├── 📂 assets/                # Modelos 3D e texturas
│   ├── 📂 Modelos3D/
│   └── 📂 tex/
├── 📂 docs/                  # Documentação e tutoriais
│   ├── GettingStarted.md
│   ├── OrganizandoRepositorioGithub.pdf
│   ├── TutorialEntregasGithub.pdf
│   └── 📂 snippets/          # LoadSimpleOBJ.cpp / .md
├── 📂 build/                 # Gerado pelo CMake (não versionado)
├── 📄 CMakeLists.txt         # Configuração do CMake
└── 📄 README.md              # Este arquivo
```

Siga as instruções detalhadas em [docs/GettingStarted.md](docs/GettingStarted.md) para configurar e compilar o projeto.

## 🎯 Entregas

- [Desafio M1](entregas/M1/README.md)
- [Desafio M2 — Cubos Interativos](entregas/M2/README.md)
- [Atividade Vivencial M2 — Multi OBJ](entregas/M2-vivencial/README.md)
- [Desafio M3 — Texturas e Materiais](entregas/M3/README.md)

## ⚠️ **IMPORTANTE: Baixar a GLAD Manualmente**
Para que o projeto funcione corretamente, é necessário **baixar a GLAD manualmente** utilizando o **GLAD Generator**.

### 🔗 **Acesse o web service do GLAD**:
👉 [GLAD Generator](https://glad.dav1d.de/)

### ⚙️ **Configuração necessária:**
- **API:** OpenGL  
- **Version:** 3.3+ (ou superior compatível com sua máquina)  
- **Profile:** Core  
- **Language:** C/C++  

### 📥 **Baixe e extraia os arquivos:**
Após a geração, extraia os arquivos baixados e coloque-os nos diretórios correspondentes:
- Copie **`glad.h`** para `include/glad/`
- Copie **`khrplatform.h`** para `include/glad/KHR/`
- Copie **`glad.c`** para `common/`

🚨 **Sem esses arquivos, a compilação falhará!** É necessário colocar esses arquivos nos diretórios corretos, conforme a orientação acima.

