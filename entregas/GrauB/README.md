# Grau B — Cena Final

Essa é a entrega que fecha a disciplina. Em vez de deixar cada desafio do semestre rodando solto num executável separado, juntei tudo num **visualizador só**: a leitura de OBJ/MTL do M3, a iluminação de Phong do M4, a câmera em primeira pessoa do M5 e a animação por trajetória do M6 — só que agora a animação virou **curva de Bézier** de verdade, e a cena inteira é montada a partir de um **arquivo de configuração** em vez de ficar chumbada no código.

A ideia é que dá pra abrir o programa, montar a cena clicando/voando pela câmera, salvar, e na próxima execução tudo volta do jeito que ficou.

Código-fonte: [`src/desafios/GrauB_CenaFinal.cpp`](../../src/desafios/GrauB_CenaFinal.cpp)
Configuração da cena: [`assets/cena.cfg`](../../assets/cena.cfg)

## O que entrou aqui (e de onde veio)

Como é a unificação, vale dizer o que cada parte herdou dos desafios anteriores:

1. **Vários OBJs com material próprio** (vem do M3). Cada objeto é triangulado, tem normais e coordenadas de textura, e carrega seu próprio material (`ka`, `kd`, `ks`, `Ns`) e textura lidos do `.mtl`.
2. **Iluminação de Phong** (vem do M4), mas agora com **3 fontes de luz** no esquema clássico key/fill/back e atenuação por distância. Dá pra mexer ao vivo na intensidade das luzes e no coeficiente especular.
3. **Câmera sintética navegável** (vem do M5) — perspectiva, controle por teclado + mouse, anda livre pela cena.
4. **Seleção e transformação de objeto** (vem do M2/M3) — translação, rotação e escala uniforme no objeto selecionado.
5. **Animação por curva paramétrica** — aqui está a parte nova em relação ao M6. Lá o objeto andava entre os waypoints por translação **linear**; agora os pontos de controle viram uma **Bézier cúbica** e o movimento fica suave.

E juntando tudo: a cena não nasce mais no código, ela é descrita no `cena.cfg`.

## O arquivo de configuração (`assets/cena.cfg`)

É um formato texto bem simples, uma palavra-chave por linha (linha começando com `#` é comentário). Quem lê isso é a função `loadScene()`. As palavras-chave são:

```
camera  px py pz   yaw pitch   fov near far
light   px py pz   r g b       intensidade  on(0/1)
object  arquivo.obj  px py pz   rx ry rz(graus)   escala
bezier  px py pz     -> ponto de controle do ÚLTIMO objeto declarado
```

O detalhe é que as linhas `bezier` se anexam sempre ao último `object` que apareceu. Com 4, 7, 10... pontos elas formam segmentos de **Bézier cúbica** encadeados — no `cena.cfg` eu uso 7 pontos no Cube, que dão 2 segmentos fechando o ciclo.

## Controles

| Tecla / Input | O que faz |
|---|---|
| W / A / S / D | Anda com a câmera |
| Space / Ctrl | Sobe / desce a câmera |
| Shift (segurar) | Anda mais rápido |
| Mouse / Scroll | Olha ao redor / zoom (FOV) |
| **TAB** | Seleciona o próximo objeto (fica realçado) |
| **M** | Cicla o modo: Translação → Rotação → Escala |
| Setas / PageUp / PageDown | Aplicam o modo atual no objeto (escala usa ↑/↓) |
| **T** | Liga/desliga a textura (material puro vs texturizado) |
| **G** | Liga/desliga wireframe |
| **1 / 2 / 3** | Liga/desliga cada luz (key / fill / back) |
| **5 / 6** | Diminui / aumenta a intensidade geral das luzes |
| **7 / 8** | Diminui / aumenta o coeficiente especular (ks) |
| **ENTER** | Play / pause da animação (Bézier) |
| **P** | Adiciona um ponto de controle na posição da câmera |
| **Backspace / C** | Remove o último ponto / limpa a trajetória do selecionado |
| **[ / ]** | Diminui / aumenta a velocidade da animação |
| **B** | Mostra/esconde a curva e os pontos de controle |
| **F2** | Salva a cena atual de volta no `cena.cfg` |
| **ESC** | Sai |

## Como rodar

Mesmo esquema dos desafios anteriores — o binário precisa rodar a partir de `build/` para achar a cena e os modelos pelo caminho relativo (`../assets/...`). No Windows com MSYS2:

```powershell
cmake -S . -B build
cmake --build build

$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd build
.\GrauB_CenaFinal.exe
```

## Onde está cada conceito no código (para a arguição)

Deixei mapeado o que apontar para cada pergunta possível:

| Pergunta do professor | Onde apontar |
|---|---|
| Parser do arquivo de configuração da cena | `loadScene()` |
| Leitura/parsing da malha (.obj) e do material (.mtl) | `loadOBJWithNormals()`, `parseMTL()` |
| Montagem da malha nos buffers (VBO/VAO) | final de `loadOBJWithNormals()` |
| Passagem de uniforms | bloco `glGetUniformLocation(...)` e as chamadas `glUniform*` no loop de `main()` |
| Manipulação das matrizes Model e View | `model = translate*rotate*scale` no loop; `camera.viewMatrix()` (lookAt) |
| Cálculo da iluminação (Phong) | laço das luzes no **fragment shader** (`fragmentShaderSource`) |
| Curva paramétrica (Bézier) | `bezierPoint()` e `rebuildCurve()` |
