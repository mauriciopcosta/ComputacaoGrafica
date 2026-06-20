# Grau B — Cena Final (visualizador unificado)

Esta é a entrega final da disciplina. Em vez de mostrar cada tarefa do semestre
separada, aqui está **um único visualizador** que integra todo o pipeline numa
cena só: leitura de vários OBJs, materiais e texturas, iluminação de Phong,
câmera sintética navegável, seleção/transformação de objetos e animação por
curva paramétrica (Bézier). A cena é descrita por um **arquivo de configuração**.

Código-fonte: [`src/desafios/GrauB_CenaFinal.cpp`](../../src/desafios/GrauB_CenaFinal.cpp)
Configuração da cena: [`assets/cena.cfg`](../../assets/cena.cfg)

## O que tem aqui (requisitos do Grau B)

1. **Vários OBJs**, já triangulados, com normais e coordenadas de textura. Cada
   objeto tem o seu material (`ka`, `kd`, `ks`, `Ns`) e textura lidos do `.mtl`.
2. **Iluminação de Phong** com **3 fontes de luz** (esquema key/fill/back) e
   atenuação por distância. Coeficientes parametrizáveis ao vivo (intensidade
   das luzes e coeficiente da especular).
3. **Câmera** controlada por teclado + mouse, com navegação livre pela cena.
4. **Seleção de objeto** e operações geométricas: translação, rotação e escala
   **uniforme**.
5. **Arquivo de configuração de cena** (`cena.cfg`): define os objetos e suas
   transformações iniciais, a animação (pontos de controle Bézier), as luzes e a
   posição/orientação inicial da câmera + frustum.

## O arquivo de configuração (`assets/cena.cfg`)

Formato texto simples, uma palavra-chave por linha (linhas `#` são comentário).
O parser está na função `loadScene()` do código.

```
camera  px py pz   yaw pitch   fov near far
light   px py pz   r g b       intensidade  on(0/1)
object  arquivo.obj  px py pz   rx ry rz(graus)   escala
bezier  px py pz     -> ponto de controle do ÚLTIMO objeto declarado
```

As linhas `bezier` se anexam ao último `object`; com 4, 7, 10... pontos elas
formam segmentos de **Bézier cúbica** encadeados (uso 7 no Cube = 2 segmentos
fechando o ciclo).

## Controles

| Tecla / Input | Ação |
|---|---|
| W A S D | Anda com a câmera |
| Space / Ctrl | Sobe / desce a câmera |
| Shift (segurar) | Anda mais rápido |
| Mouse / Scroll | Olhar em volta / zoom (FOV) |
| **TAB** | Seleciona o próximo objeto (realçado) |
| **M** | Cicla o modo: Translação → Rotação → Escala |
| Setas / PageUp / PageDown | Aplicam o modo atual no objeto (escala usa ↑/↓) |
| **T** | Liga/desliga a textura (mostra material puro vs texturizado) |
| **G** | Liga/desliga wireframe |
| **1 / 2 / 3** | Liga/desliga cada luz (key / fill / back) |
| **5 / 6** | Diminui / aumenta a intensidade geral das luzes |
| **7 / 8** | Diminui / aumenta o coeficiente especular (ks) |
| **ENTER** | Play / pause da animação (Bézier) |
| **P** | Adiciona ponto de controle na posição da câmera |
| **Backspace / C** | Remove o último ponto / limpa a trajetória do selecionado |
| **[ / ]** | Diminui / aumenta a velocidade da animação |
| **B** | Mostra/esconde a curva e os pontos de controle |
| **F2** | Salva a cena atual de volta no `cena.cfg` |
| **ESC** | Sai |

## Como compilar e rodar

```powershell
cmake -S . -B build
cmake --build build

$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd build
.\GrauB_CenaFinal.exe
```

> O binário precisa rodar de dentro de `build/` porque a cena e os modelos são
> lidos por caminho relativo (`../assets/...`).

## Onde está cada conceito no código (para a arguição)

| Pergunta do professor | Onde apontar |
|---|---|
| Parser do arquivo de configuração da cena | `loadScene()` |
| Leitura/parsing da malha (.obj) e do material (.mtl) | `loadOBJWithNormals()`, `parseMTL()` |
| Montagem da malha nos buffers (VBO/VAO) | final de `loadOBJWithNormals()` |
| Passagem de uniforms | bloco `glGetUniformLocation(...)` e as chamadas `glUniform*` no loop de `main()` |
| Manipulação das matrizes Model e View | `model = translate*rotate*scale` no loop; `camera.viewMatrix()` (lookAt) |
| Cálculo da iluminação (Phong) | laço das luzes no **fragment shader** (`fragmentShaderSource`) |
| Curva paramétrica (Bézier) | `bezierPoint()` e `rebuildCurve()` |

O roteiro detalhado da defesa está em
[`ROTEIRO_DEFESA.md`](ROTEIRO_DEFESA.md).
