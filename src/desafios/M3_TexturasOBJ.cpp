/* Desafio Modulo 3 - Texturas e Materiais
 *
 * Mauricio Pereira da Costa - Computacao Grafica (Unisinos)
 *
 * Peguei o visualizador Multi-OBJ do M2-vivencial e fiz ele sair de "uma cor
 * por objeto" pra mostrar TEXTURA de verdade. Pra isso eu tive que:
 *  - ler tambem o "vt" (coordenada de textura) do .obj - o loader antigo
 *    jogava fora, agora ele entra no vertice (que passou de 6 pra 8 floats);
 *  - achar o nome da imagem: o .obj diz "mtllib Suzanne.mtl", entao eu abro
 *    esse .mtl e pego a linha "map_Kd <arquivo>";
 *  - carregar a imagem com a stb_image e amostrar ela no fragment shader.
 *
 * Quem nao tem textura (o Cube.mtl é vazio) eu nao descarto: ele continua na
 * cena usando a cor do vertice. Quem decide isso é a flag hasTexture no shader.
 *
 * Uma pegadinha que me custou tempo: a textura abria de cabeça pra baixo, e era
 * porque a stb lê a imagem do topo pra baixo e o .obj usa o contrário. Resolvi
 * com stbi_set_flip_vertically_on_load(true).
 *
 * Controles: iguais ao M2-vivencial
 *   TAB                cicla selecao
 *   T / R / S          modos Translacao / Rotacao / Escala
 *   Em Translacao:     setas + PageUp/PageDown
 *   Em Rotacao:        X/Y/Z togglam giro no eixo
 *   Em Escala:         X/Y/Z (Shift inverte) e [ ] (uniforme)
 *   L                  liga/desliga wireframe
 *   ESC                sai
 */

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

using namespace std;
namespace fs = std::filesystem;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void framebuffer_size_callback(GLFWwindow* window, int w, int h);
GLuint setupShader();
GLuint loadTexture(const string& filePath);
string  parseMTLForDiffuseMap(const string& mtlPath);

const GLuint WIDTH = 1400, HEIGHT = 900;

// Shader e location da projection ficam acessiveis pelo callback de resize
// para reenviar a matriz quando a janela muda de tamanho.
static GLuint gShaderID = 0;
static GLint  gProjLoc  = -1;

// Vertex shader: a novidade é o location 2 (tex_coord). Eu só recebo a coord
// de textura e repasso ela (vTexCoord) pro fragment - quem realmente vai usar
// pra "buscar o pixel" na imagem é o fragment shader.
const GLchar* vertexShaderSource = R"(
#version 450
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec2 tex_coord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vColor;
out vec2 vTexCoord;
void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
    vColor    = color;
    vTexCoord = tex_coord;
}
)";

// Fragment shader. A linha principal é o "texture(tex_buffer, vTexCoord)": é
// ela que pega a cor do pixel da imagem na coordenada que veio do vertice.
// Se hasTexture==1 uso essa cor; se não, caio na cor do vertice (vColor). O
// tint eu mantive do M2-vivencial pra destacar o selecionado.
const GLchar* fragmentShaderSource = R"(
#version 450
in vec3 vColor;
in vec2 vTexCoord;
uniform sampler2D tex_buffer;
uniform int   hasTexture;
uniform int   wireframe;
uniform float tint;
out vec4 color;
void main() {
    if (wireframe == 1) {
        color = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    vec3 base = (hasTexture == 1) ? texture(tex_buffer, vTexCoord).rgb : vColor;
    color = vec4(clamp(base * tint, 0.0, 1.0), 1.0);
}
)";

struct OBJ {
    string name;
    GLuint VAO = 0;
    int    nVertices = 0;
    GLuint texID = 0;
    bool   hasTexture = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);
    glm::vec3 angles   = glm::vec3(0.0f);
    glm::ivec3 rotOn   = glm::ivec3(0);
};

vector<OBJ> objects;
int selected = 0;

enum class Mode { Translate, Rotate, Scale };
Mode currentMode = Mode::Translate;
bool showWireframe = false; // padrao desligado para nao tapar a textura

const float MOVE_SPEED  = 1.5f;
const float ROT_SPEED   = 1.5f;
const float SCALE_SPEED = 1.5f;

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::Translate: return "TRANSLACAO";
        case Mode::Rotate:    return "ROTACAO";
        case Mode::Scale:     return "ESCALA";
    }
    return "?";
}

// Loader do .obj - igual ao do M2-vivencial, mas agora cada vertice tem 8
// floats: xyz (posicao) + rgb (cor) + st (textura). A cor eu deixo branca de
// proposito: quando tem textura, o branco "não suja" a imagem (branco * cor =
// a propria cor); quando não tem, o objeto fica branco neutro.
GLuint loadOBJWithTexCoords(const string& filePATH, int& nVertices, string& mtllibOut) {
    vector<glm::vec3> vertices;
    vector<glm::vec2> texCoords;
    vector<glm::vec3> normals;
    vector<GLfloat>   vBuffer;
    mtllibOut.clear();

    ifstream arq(filePATH.c_str());
    if (!arq.is_open()) {
        cerr << "Erro ao abrir: " << filePATH << endl;
        nVertices = 0;
        return 0;
    }

    const glm::vec3 baseColor(1.0f);

    string line;
    while (getline(arq, line)) {
        istringstream ss(line);
        string word;
        ss >> word;

        if (word == "mtllib") {
            ss >> mtllibOut;            // guardo o nome do .mtl pra abrir depois
        } else if (word == "v") {
            glm::vec3 v; ss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        } else if (word == "vt") {
            glm::vec2 vt; ss >> vt.s >> vt.t;
            texCoords.push_back(vt);    // <- agora eu NÃO jogo fora o vt
        } else if (word == "vn") {
            glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        } else if (word == "f") {
            // Quebro o "v/vt/vn" e dessa vez pego tambem o ti (indice da textura).
            while (ss >> word) {
                int vi = -1, ti = -1, ni = -1;
                istringstream tok(word);
                string idx;
                if (getline(tok, idx, '/')) vi = !idx.empty() ? stoi(idx) - 1 : -1;
                if (getline(tok, idx, '/')) ti = !idx.empty() ? stoi(idx) - 1 : -1;
                if (getline(tok, idx))      ni = !idx.empty() ? stoi(idx) - 1 : -1;
                (void)ni;  // a normal só vai ser usada no M4

                if (vi < 0 || vi >= (int)vertices.size()) continue;

                // Pego o (s,t) que esse vertice da face aponta (ou 0,0 se faltar).
                glm::vec2 vt(0.0f);
                if (ti >= 0 && ti < (int)texCoords.size()) vt = texCoords[ti];

                vBuffer.push_back(vertices[vi].x);
                vBuffer.push_back(vertices[vi].y);
                vBuffer.push_back(vertices[vi].z);
                vBuffer.push_back(baseColor.r);
                vBuffer.push_back(baseColor.g);
                vBuffer.push_back(baseColor.b);
                vBuffer.push_back(vt.s);
                vBuffer.push_back(vt.t);
            }
        }
    }
    arq.close();

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    // Agora o vertice tem 8 floats, então o stride mudou. Aponto os 3 atributos:
    // posição (0..2), cor (3..5) e a coord de textura nova no location 2 (6..7).
    const GLsizei stride = 8 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = (int)(vBuffer.size() / 8);
    return VAO;
}

// Parser do .mtl - bem mininalista de proposito: o enunciado só pedia o NOME
// da textura, então eu varro o arquivo procurando a linha "map_Kd" e devolvo o
// que vier depois. Se não achar, volto string vazia (= objeto sem textura).
string parseMTLForDiffuseMap(const string& mtlPath) {
    ifstream arq(mtlPath.c_str());
    if (!arq.is_open()) return "";
    string line;
    while (getline(arq, line)) {
        istringstream ss(line);
        string word; ss >> word;
        if (word == "map_Kd") {
            string tex; ss >> tex;
            return tex;
        }
    }
    return "";
}

// Carrega uma imagem do disco e sobe pra GPU como textura. Segui o passo a
// passo do TriangleTex (material de apoio): cria o id, define wrap/filtros,
// le os pixels com a stb e manda pro OpenGL com glTexImage2D + mipmaps.
GLuint loadTexture(const string& filePath) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    // Wrap = o que fazer fora de [0,1] (repetir). Filtros = como suavizar quando
    // a textura aparece maior/menor que a imagem. Uso mipmap na minificação
    // porque os modelos ficam pequenos na cena e sem isso a textura "fervilha".
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    // OBJ/Blender exportam vt com origem no canto inferior-esquerdo, igual a
    // convencao do OpenGL. stb_image carrega a imagem com a primeira linha no
    // topo; portanto invertemos verticalmente para alinhar com o vt.
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 0);
    if (data) {
        GLenum fmt = (nrChannels == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura carregada: " << filePath << " (" << width << "x" << height
             << ", " << nrChannels << " canais)" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID);
        texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

int main() {
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(
        WIDTH, HEIGHT,
        "Desafio M3 - Texturas em OBJs - Mauricio Pereira da Costa",
        nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);
    gShaderID = shaderID;

    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
    GLint tintLoc       = glGetUniformLocation(shaderID, "tint");
    GLint wireframeLoc  = glGetUniformLocation(shaderID, "wireframe");
    GLint hasTextureLoc = glGetUniformLocation(shaderID, "hasTexture");
    gProjLoc = projLoc;

    // Digo pro shader que o sampler "tex_buffer" vai ler da unidade de textura 0.
    // Como eu só uso uma textura por vez, deixo a unidade 0 ativa o tempo todo.
    glUniform1i(glGetUniformLocation(shaderID, "tex_buffer"), 0);
    glActiveTexture(GL_TEXTURE0);

    // Camera mais afastada (z=9) para os 3 modelos caberem inteiros no
    // viewport inicial sem precisar mexer na escala dos objetos.
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 9.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glEnable(GL_DEPTH_TEST);

    const string modelsDir = "../assets/Modelos3D";
    if (!fs::exists(modelsDir)) {
        cerr << "Pasta nao encontrada: " << modelsDir
             << "\n(execute o binario a partir da pasta build/)" << endl;
        return -1;
    }

    vector<fs::path> objPaths;
    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".obj")
            objPaths.push_back(entry.path());
    }
    sort(objPaths.begin(), objPaths.end());

    if (objPaths.empty()) {
        cerr << "Nenhum .obj em " << modelsDir << endl;
        return -1;
    }

    int idx = 0;
    float spacing = 2.2f;
    float startX  = -spacing * (objPaths.size() - 1) * 0.5f;
    for (const auto& p : objPaths) {
        OBJ o;
        o.name = p.filename().string();

        // Carrego a malha; de quebra o loader me diz qual .mtl o .obj referencia.
        string mtllib;
        o.VAO = loadOBJWithTexCoords(p.string(), o.nVertices, mtllib);
        if (o.VAO == 0) { idx++; continue; }

        // Se tem .mtl, abro ele (ao lado do .obj), pego o map_Kd e resolvo o
        // caminho da imagem relativo ao .mtl. Aí carrego a textura.
        if (!mtllib.empty()) {
            fs::path mtlPath = p.parent_path() / mtllib;
            string mapKd = parseMTLForDiffuseMap(mtlPath.string());
            if (!mapKd.empty()) {
                fs::path texPath = mtlPath.parent_path() / mapKd;
                o.texID = loadTexture(texPath.string());
                o.hasTexture = (o.texID != 0);
            } else {
                cout << "Aviso: " << mtllib << " nao tem map_Kd (objeto sem textura)\n";
            }
        }

        o.position = glm::vec3(startX + spacing * idx, 0.0f, 0.0f);
        o.scale    = glm::vec3(0.6f);
        objects.push_back(o);
        cout << "[" << objects.size() - 1 << "] " << o.name
             << " (" << o.nVertices << " vertices"
             << (o.hasTexture ? ", com textura" : ", sem textura") << ")" << endl;
        idx++;
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado com sucesso." << endl;
        return -1;
    }

    cout << "\nControles: TAB cicla | T translacao | R rotacao | S escala | L wireframe | ESC sai\n";
    cout << "Modo inicial: " << modeName(currentMode) << endl;
    cout << "Selecionado: [" << selected << "] " << objects[selected].name << endl;

    float lastTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;

        OBJ& sel = objects[selected];

        if (currentMode == Mode::Translate) {
            if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) sel.position.x -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) sel.position.x += MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) sel.position.y += MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) sel.position.y -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) sel.position.z -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) sel.position.z += MOVE_SPEED * dt;
        }
        else if (currentMode == Mode::Scale) {
            bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS
                      || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            float dir = shift ? -1.0f : 1.0f;
            float factor = 1.0f + dir * SCALE_SPEED * dt;

            if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) sel.scale.x *= factor;
            if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) sel.scale.y *= factor;
            if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) sel.scale.z *= factor;

            if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_MINUS)         == GLFW_PRESS)
                sel.scale *= (1.0f - SCALE_SPEED * dt);
            if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_EQUAL)         == GLFW_PRESS)
                sel.scale *= (1.0f + SCALE_SPEED * dt);

            sel.scale = glm::max(sel.scale, glm::vec3(0.05f));
        }

        for (auto& o : objects) {
            if (o.rotOn.x) o.angles.x += ROT_SPEED * dt;
            if (o.rotOn.y) o.angles.y += ROT_SPEED * dt;
            if (o.rotOn.z) o.angles.z += ROT_SPEED * dt;
        }

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (size_t i = 0; i < objects.size(); ++i) {
            OBJ& o = objects[i];
            glm::mat4 model(1.0f);
            model = glm::translate(model, o.position);
            model = glm::rotate(model, o.angles.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, o.angles.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, o.angles.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, o.scale);

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glBindVertexArray(o.VAO);

            // Conecta a textura do objeto (zero se nao tiver — hasTexture=0 faz
            // o shader cair no caminho da cor por vertice).
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, o.texID);
            glUniform1i(hasTextureLoc, o.hasTexture ? 1 : 0);

            if (showWireframe) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 1.0f);
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUniform1i(wireframeLoc, 0);
            glUniform1f(tintLoc, ((int)i == selected) ? 1.4f : 0.85f);
            glDrawArrays(GL_TRIANGLES, 0, o.nVertices);
            if (showWireframe) glDisable(GL_POLYGON_OFFSET_FILL);

            if (showWireframe) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(1.0f);
                glUniform1i(wireframeLoc, 1);
                glDrawArrays(GL_TRIANGLES, 0, o.nVertices);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        glBindVertexArray(0);

        glfwSwapBuffers(window);
    }

    for (auto& o : objects) {
        glDeleteVertexArrays(1, &o.VAO);
        if (o.texID) glDeleteTextures(1, &o.texID);
    }
    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* /*window*/, int w, int h) {
    if (h <= 0) return;
    glViewport(0, 0, w, h);
    if (gShaderID == 0 || gProjLoc < 0) return;
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);
    glUseProgram(gShaderID);
    glUniformMatrix4fv(gProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }
    if (objects.empty()) return;

    if (key == GLFW_KEY_TAB) {
        selected = (selected + 1) % (int)objects.size();
        cout << "Selecionado: [" << selected << "] " << objects[selected].name << endl;
        return;
    }

    if (key == GLFW_KEY_T) { currentMode = Mode::Translate; cout << "Modo: " << modeName(currentMode) << endl; return; }
    if (key == GLFW_KEY_R) { currentMode = Mode::Rotate;    cout << "Modo: " << modeName(currentMode) << endl; return; }
    if (key == GLFW_KEY_S) { currentMode = Mode::Scale;     cout << "Modo: " << modeName(currentMode) << endl; return; }

    if (key == GLFW_KEY_L) {
        showWireframe = !showWireframe;
        cout << "Wireframe: " << (showWireframe ? "ON" : "OFF") << endl;
        return;
    }

    if (currentMode == Mode::Rotate) {
        OBJ& o = objects[selected];
        if (key == GLFW_KEY_X) o.rotOn.x = 1 - o.rotOn.x;
        if (key == GLFW_KEY_Y) o.rotOn.y = 1 - o.rotOn.y;
        if (key == GLFW_KEY_Z) o.rotOn.z = 1 - o.rotOn.z;
    }
}

GLuint setupShader() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    GLint ok; GLchar log[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); cout << "VS:\n" << log << endl; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); cout << "FS:\n" << log << endl; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); cout << "LINK:\n" << log << endl; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}
