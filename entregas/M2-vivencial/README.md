# Atividade Vivencial — Módulo 2

Extensão do M2. A proposta foi tirar o cubo hardcoded e fazer o programa ler todos os `.obj` que estiverem dentro de `assets/Modelos3D/`, mostrando todos juntos na cena. Hoje a pasta tem o Cube, a Suzanne padrão e a SuzanneSubdiv1 — então o programa abre com os três alinhados no eixo X, cada um com uma cor da paleta.

Pra carregar os arquivos eu reaproveitei o `loadSimpleOBJ` da professora (tava no `Code snippets/`), só adaptei pra receber a cor por parâmetro — assim cada modelo recebe uma cor diferente em vez de todo mundo vermelho. A enumeração dos arquivos eu fiz com `std::filesystem::directory_iterator`, filtrando só os `.obj` e ordenando alfabeticamente pra ficar previsível. Como o CMake já estava em C++17, não precisei mexer em nada da build.

A parte das transformações mudou de abordagem em relação ao M2. Antes as teclas X/Y/Z togglavam rotação direto; agora tem um conceito de **modo de operação** que muda com T (translação), R (rotação) e S (escala). As mesmas teclas se comportam diferente dependendo do modo — em translação as setas e PageUp/Down movem; em rotação X/Y/Z togglam giro; em escala X/Y/Z aumentam (Shift inverte pra diminuir) e `[ ]` faz escala uniforme. Usei modo porque com 3 tipos de transformação ficava difícil arrumar teclas sem conflito (S, por exemplo, ia colidir com WASD se fosse atalho direto).

Pra deixar claro qual objeto tá selecionado, o fragment shader ganhou um `uniform float tint`: quem tá selecionado vai com 1.6× a cor, o resto com 0.85× — então o ativo "brilha" e os outros ficam apagadinhos. TAB cicla a seleção.

## Controles

| Tecla              | O que faz                                          |
|--------------------|----------------------------------------------------|
| TAB                | Próximo objeto                                     |
| T                  | Entra em modo Translação                           |
| R                  | Entra em modo Rotação                              |
| S                  | Entra em modo Escala                               |
| L                  | Liga/desliga arestas brancas (em todos os objetos) |
| ESC                | Sai                                                |
| **Em Translação**  |                                                    |
| ← / →              | Translada em X                                     |
| ↑ / ↓              | Translada em Y                                     |
| PageUp / PageDown  | Translada em Z                                     |
| **Em Rotação**     |                                                    |
| X / Y / Z          | Toggle giro contínuo no eixo                       |
| **Em Escala**      |                                                    |
| X / Y / Z          | Aumenta no eixo (Shift = diminui)                  |
| `[` / `]` ou `-` / `=` | Escala uniforme (diminui / aumenta)            |

## Como rodar

Os assets são lidos por caminho relativo (`../assets/Modelos3D`), então o `.exe` precisa ser executado de dentro de `build/`. No Windows com MSYS2:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd build
.\M2_Vivencial_MultiOBJ.exe
```

(o `$env:PATH` é pras DLLs do GCC; sem ele o exe fecha sozinho)

Código-fonte: [`src/desafios/M2_Vivencial_MultiOBJ.cpp`](../../src/desafios/M2_Vivencial_MultiOBJ.cpp)
