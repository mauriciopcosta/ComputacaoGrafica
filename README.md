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
- [Desafio M4 — Iluminação (Phong)](entregas/M4/README.md)
- [Atividade Vivencial M4 — Iluminação de 3 Pontos](entregas/M4-vivencial/README.md)
- [Desafio M5 — Câmera em 1ª Pessoa](entregas/M5/README.md)
- [Desafio M6 — Trajetórias](entregas/M6/README.md)
- [**Grau B — Cena Final (visualizador unificado)**](entregas/GrauB/README.md)

## 🏁 Entrega Final (Grau B)

O visualizador final integra tudo num único programa: leitura de **vários OBJs**
(com material e textura do `.mtl`), **iluminação de Phong** com 3 luzes
liga/desliga, **câmera navegável**, **seleção e transformação** de objetos
(translação, rotação e escala uniforme) e **animação por curva de Bézier**. A
cena é montada a partir de um **arquivo de configuração** ([assets/cena.cfg](assets/cena.cfg)).

- Código-fonte: [src/desafios/GrauB_CenaFinal.cpp](src/desafios/GrauB_CenaFinal.cpp)
- Detalhes, controles e roteiro de defesa: [entregas/GrauB/README.md](entregas/GrauB/README.md)

### ⚙️ Setup (compilação e execução)

Dependências resolvidas automaticamente pelo CMake via `FetchContent`: **GLFW 3.4**,
**GLM** e **stb_image**. A **GLAD** (OpenGL 3.3+ Core) precisa ser baixada manualmente
(ver seção abaixo). Compilador com **C++17**.

```powershell
# 1) Configurar e compilar (a partir da raiz do projeto)
cmake -S . -B build
cmake --build build

# 2) Executar a cena final (precisa rodar de dentro de build/ por causa
#    dos caminhos relativos ../assets/...)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"   # DLLs do GCC (MSYS2/Windows)
cd build
.\GrauB_CenaFinal.exe
```

### 🖼️ Assets (procedência)

- **Modelos 3D** (`assets/Modelos3D/`): `Cube`, `Suzanne` e `SuzanneSubdiv1` —
  malhas do material de apoio da disciplina (Suzanne é o macaco padrão do
  **Blender**). Já vêm trianguladas, com normais (`vn`) e coordenadas de textura (`vt`).
- **Texturas** (`assets/Modelos3D/Suzanne.png`, `SuzanneUV.png`, `assets/tex/pixelWall.png`):
  mapas de cor (color map) que acompanham os modelos do material da disciplina.
- Processamento prévio: os `.obj`/`.mtl` foram usados como vieram do material;
  nenhum reprocessamento adicional em Blender/MeshLab foi necessário.

### 📚 Referências

- Joey de Vries — **LearnOpenGL** (https://learnopengl.com): câmera, iluminação,
  texturas, transformações.
- **Documentação OpenGL** (docs.gl) e **GLFW**/**GLM** (docs oficiais).
- Material de apoio e snippets da disciplina (profª Rossana Baptista Queiroz):
  `LoadSimpleOBJ`, `TriangleTex`, `SpherePhong`.

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

