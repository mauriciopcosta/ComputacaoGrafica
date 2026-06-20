/* Desafio Modulo 4 - Iluminacao (Modelo de Phong)
 *
 * Mauricio Pereira da Costa - Computacao Grafica (Unisinos)
 *
 * Aqui a cena finalmente ganhou LUZ. Ate o M3 a cor era "chapada"; agora eu
 * implementei o modelo de Phong (ambiente + difusa + especular) com uma fonte
 * de luz pontual. O que eu precisei fazer:
 *  - ler a normal (vn) do .obj - sem ela não dá pra calcular iluminação. Com
 *    isso o vertice foi pra 11 floats: posicao + cor + textura + NORMAL;
 *  - ler do .mtl os coeficientes do material: Ka (ambiente), Kd (difusa),
 *    Ks (especular) e Ns (o "brilho" q da especular), e mandar tudo pro shader;
 *  - fazer a conta do Phong por pixel lá no fragment shader.
 *
 * A cor base continua vindo da textura (ou da cor do vertice, no fallback) - a
 * iluminação multiplica em cima dessa cor.
 *
 * Deixei a luz se mexer pelo teclado (I/J/K/L/U/O) só pra eu conseguir mostrar
 * a especular/difusa de vários ângulos sem ter que girar o objeto.
 *
 * Controles (herdados do M3):
 *   TAB                cicla selecao
 *   T / R / S          modos Translacao / Rotacao / Escala
 *   Em Translacao:     setas + PageUp/PageDown
 *   Em Rotacao:        X/Y/Z togglam giro no eixo
 *   Em Escala:         X/Y/Z (Shift inverte) e [ ] (uniforme)
 *   Setas da luz:      I/J/K/L movem a fonte de luz em X/Y; U/O em Z
 *   L (com Ctrl)       -> reservado; use a tecla G para wireframe
 *   G                  liga/desliga wireframe
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

const GLuint WIDTH = 1400, HEIGHT = 900;

static GLuint gShaderID = 0;
static GLint  gProjLoc  = -1;

// Vertex shader. Pra iluminação funcionar, o fragment precisa de duas coisas
// que eu calculo aqui e mando pra frente:
//  - fragPos: a posição do vértice JÁ no espaço do mundo (model * posição).
//    É com ela que eu meço a direção da luz e da câmera lá no fragment.
//  - vNormal: a normal também levada pro mundo. Importante: uso
//    mat3(transpose(inverse(model))) e não só "model", senão a normal fica
//    torta quando o objeto recebe escala diferente em cada eixo.
const GLchar* vertexShaderSource = R"(
#version 450
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec2 tex_coord;
layout (location = 3) in vec3 normal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vColor;
out vec2 vTexCoord;
out vec3 fragPos;
out vec3 vNormal;
void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
    vColor    = color;
    vTexCoord = tex_coord;
    fragPos   = vec3(model * vec4(position, 1.0));
    // matriz normal: corrige a normal sob escala nao-uniforme
    vNormal   = mat3(transpose(inverse(model))) * normal;
}
)";

// Fragment shader: É AQUI que mora a conta da iluminação (modelo de Phong),
// feita por pixel. A fórmula que eu montei é:
//   I = (ambiente + difusa) * cor_do_objeto + especular
// ambiente  -> luz de "fundo" que bate em tudo igual;
// difusa    -> depende do ângulo entre a normal (N) e a direção da luz (L):
//              quanto mais de frente a luz bate, mais claro (dot(N,L));
// especular -> o brilho/reflexo: reflito a luz (R) e comparo com a direção pra
//              câmera (V); elevo a q pra concentrar o brilho num ponto.
// Os coeficientes ka/kd/ks/q vêm do .mtl; a cor base vem da textura ou do vColor.
const GLchar* fragmentShaderSource = R"(
#version 450
in vec3 vColor;
in vec2 vTexCoord;
in vec3 fragPos;
in vec3 vNormal;

uniform sampler2D tex_buffer;
uniform int   hasTexture;
uniform int   wireframe;
uniform float tint;

// Propriedades do material (vindas do .mtl)
uniform float ka;
uniform float kd;
uniform float ks;
uniform float q;

// Fonte de luz pontual e camera
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 cameraPos;
uniform float ambientStrength; // intensidade da luz ambiente (Ia)

out vec4 color;

void main() {
    if (wireframe == 1) {
        color = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 objectColor = (hasTexture == 1) ? texture(tex_buffer, vTexCoord).rgb : vColor;

    // 1) Ambiente: uma luzinha de base. ambientStrength eu pus pequeno (0.2)
    //    senão o Ka do material (que vem 1,1,1) estoura tudo e mata o 3D.
    vec3 ambient = ambientStrength * ka * lightColor;

    // 2) Difusa: normalizo a normal (N) e calculo a direção até a luz (L).
    //    O dot(N,L) dá quão "de frente" a luz bate. max(...,0) corta o que
    //    está nas costas (não tem luz negativa).
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightPos - fragPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = kd * diff * lightColor;

    // 3) Especular: V é a direção daqui pro olho (câmera). R é a luz refletida
    //    na superfície. Quando R e V quase coincidem (estou vendo o reflexo)
    //    aparece o brilho. O pow(...,q) deixa esse brilho mais concentrado.
    vec3 V = normalize(cameraPos - fragPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), q);
    vec3 specular = ks * spec * lightColor;

    // Junto tudo. Repara que a especular NÃO multiplica a cor do objeto - é o
    // brilho da própria luz (por isso vai "puro", branco). clamp pra não estourar.
    vec3 result = (ambient + diffuse) * objectColor + specular;
    color = vec4(clamp(result * tint, 0.0, 1.0), 1.0);
}
)";

// Coeficientes de iluminação do objeto. Os valores aqui são meus DEFAULTS:
// se o .mtl não tiver alguma linha (ex.: o Cube.mtl é vazio), o objeto usa esses.
struct Material {
    float ka = 0.1f;   // o quanto ele reage à luz ambiente
    float kd = 0.7f;   // o quanto ele reage à luz difusa (o "corpo" da cor)
    float ks = 0.5f;   // o quanto ele brilha (especular)
    float q  = 32.0f;  // tamanho do brilho: q alto = brilho pequeno e concentrado
};

struct OBJ {
    string name;
    GLuint VAO = 0;
    int    nVertices = 0;
    GLuint texID = 0;
    bool   hasTexture = false;
    Material material;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);
    glm::vec3 angles   = glm::vec3(0.0f);
    glm::ivec3 rotOn   = glm::ivec3(0);
};

vector<OBJ> objects;
int selected = 0;

enum class Mode { Translate, Rotate, Scale };
Mode currentMode = Mode::Translate;
bool showWireframe = false;

// Fonte de luz pontual (movel pelo teclado)
glm::vec3 lightPos   = glm::vec3(2.0f, 3.0f, 4.0f);
glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

const float MOVE_SPEED  = 1.5f;
const float ROT_SPEED   = 1.5f;
const float SCALE_SPEED = 1.5f;
const float LIGHT_SPEED  = 3.0f;

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::Translate: return "TRANSLACAO";
        case Mode::Rotate:    return "ROTACAO";
        case Mode::Scale:     return "ESCALA";
    }
    return "?";
}

// Loader do .obj, agora com normal: cada vertice tem 11 floats =
// xyz (posicao) + rgb (cor) + st (textura) + xyz (NORMAL). É basicamente o
// loader do M3 com o "vn" entrando no buffer.
GLuint loadOBJWithNormals(const string& filePATH, int& nVertices, string& mtllibOut) {
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
            ss >> mtllibOut;
        } else if (word == "v") {
            glm::vec3 v; ss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        } else if (word == "vt") {
            glm::vec2 vt; ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        } else if (word == "vn") {
            glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        } else if (word == "f") {
            while (ss >> word) {
                int vi = -1, ti = -1, ni = -1;
                istringstream tok(word);
                string idx;
                if (getline(tok, idx, '/')) vi = !idx.empty() ? stoi(idx) - 1 : -1;
                if (getline(tok, idx, '/')) ti = !idx.empty() ? stoi(idx) - 1 : -1;
                if (getline(tok, idx))      ni = !idx.empty() ? stoi(idx) - 1 : -1;

                if (vi < 0 || vi >= (int)vertices.size()) continue;

                glm::vec2 vt(0.0f);
                if (ti >= 0 && ti < (int)texCoords.size()) vt = texCoords[ti];

                glm::vec3 vn(0.0f, 0.0f, 1.0f);
                if (ni >= 0 && ni < (int)normals.size()) vn = normals[ni];

                vBuffer.push_back(vertices[vi].x);
                vBuffer.push_back(vertices[vi].y);
                vBuffer.push_back(vertices[vi].z);
                vBuffer.push_back(baseColor.r);
                vBuffer.push_back(baseColor.g);
                vBuffer.push_back(baseColor.b);
                vBuffer.push_back(vt.s);
                vBuffer.push_back(vt.t);
                vBuffer.push_back(vn.x);
                vBuffer.push_back(vn.y);
                vBuffer.push_back(vn.z);
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

    const GLsizei stride = 11 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(8 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = (int)(vBuffer.size() / 11);
    return VAO;
}

// Parser do .mtl, agora completo: além do map_Kd (textura) eu leio os
// coeficientes Ka/Kd/Ks e o Ns. No arquivo Ka/Kd/Ks vêm como 3 números (RGB),
// mas no meu shader uso um escalar só, então tiro a média dos 3 canais (avg3).
// O que o arquivo não tiver, fica com o default da struct.
void parseMTL(const string& mtlPath, Material& matOut, string& diffuseMapOut) {
    diffuseMapOut.clear();
    ifstream arq(mtlPath.c_str());
    if (!arq.is_open()) return;

    auto avg3 = [](istringstream& ss) {
        float r = 0, g = 0, b = 0;
        ss >> r >> g >> b;
        return (r + g + b) / 3.0f;
    };

    string line;
    while (getline(arq, line)) {
        istringstream ss(line);
        string word; ss >> word;
        if (word == "Ka")      matOut.ka = avg3(ss);
        else if (word == "Kd") matOut.kd = avg3(ss);
        else if (word == "Ks") matOut.ks = avg3(ss);
        else if (word == "Ns") ss >> matOut.q;
        else if (word == "map_Kd") ss >> diffuseMapOut;
    }
    // Ns no MTL costuma ser alto (ex.: 233); mantemos para o brilho concentrado.
    if (matOut.q < 1.0f) matOut.q = 1.0f;
}

GLuint loadTexture(const string& filePath) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
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
        "Desafio M4 - Iluminacao de Phong - Mauricio Pereira da Costa",
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
    GLint kaLoc         = glGetUniformLocation(shaderID, "ka");
    GLint kdLoc         = glGetUniformLocation(shaderID, "kd");
    GLint ksLoc         = glGetUniformLocation(shaderID, "ks");
    GLint qLoc          = glGetUniformLocation(shaderID, "q");
    GLint lightPosLoc   = glGetUniformLocation(shaderID, "lightPos");
    GLint lightColorLoc = glGetUniformLocation(shaderID, "lightColor");
    GLint cameraPosLoc  = glGetUniformLocation(shaderID, "cameraPos");
    GLint ambStrLoc     = glGetUniformLocation(shaderID, "ambientStrength");
    gProjLoc = projLoc;

    glUniform1i(glGetUniformLocation(shaderID, "tex_buffer"), 0);
    glActiveTexture(GL_TEXTURE0);

    glm::vec3 cameraPos = glm::vec3(0.0f, 1.5f, 9.0f);
    glm::mat4 view = glm::lookAt(cameraPos,
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glUniform3fv(cameraPosLoc,  1, glm::value_ptr(cameraPos));
    glUniform3fv(lightColorLoc, 1, glm::value_ptr(lightColor));
    glUniform1f(ambStrLoc, 0.2f);

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

        string mtllib;
        o.VAO = loadOBJWithNormals(p.string(), o.nVertices, mtllib);
        if (o.VAO == 0) { idx++; continue; }

        if (!mtllib.empty()) {
            fs::path mtlPath = p.parent_path() / mtllib;
            string mapKd;
            parseMTL(mtlPath.string(), o.material, mapKd);
            if (!mapKd.empty()) {
                fs::path texPath = mtlPath.parent_path() / mapKd;
                o.texID = loadTexture(texPath.string());
                o.hasTexture = (o.texID != 0);
            }
        }

        o.position = glm::vec3(startX + spacing * idx, 0.0f, 0.0f);
        o.scale    = glm::vec3(0.6f);
        objects.push_back(o);
        cout << "[" << objects.size() - 1 << "] " << o.name
             << " (" << o.nVertices << " vertices"
             << (o.hasTexture ? ", com textura" : ", sem textura")
             << " | ka=" << o.material.ka << " kd=" << o.material.kd
             << " ks=" << o.material.ks << " q=" << o.material.q << ")" << endl;
        idx++;
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado com sucesso." << endl;
        return -1;
    }

    cout << "\nControles: TAB cicla | T/R/S modos | I/J/K/L/U/O movem a luz | G wireframe | ESC sai\n";
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

        // Movimento da fonte de luz (sempre ativo): I/K em Y, J/L em X, U/O em Z
        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) lightPos.x -= LIGHT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) lightPos.x += LIGHT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) lightPos.y += LIGHT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) lightPos.y -= LIGHT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) lightPos.z -= LIGHT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) lightPos.z += LIGHT_SPEED * dt;

        for (auto& o : objects) {
            if (o.rotOn.x) o.angles.x += ROT_SPEED * dt;
            if (o.rotOn.y) o.angles.y += ROT_SPEED * dt;
            if (o.rotOn.z) o.angles.z += ROT_SPEED * dt;
        }

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // A luz pode ter andado neste frame, então reenvio a posição dela pro
        // shader antes de desenhar (a cor da luz e a câmera não mudam aqui).
        glUniform3fv(lightPosLoc, 1, glm::value_ptr(lightPos));

        for (size_t i = 0; i < objects.size(); ++i) {
            OBJ& o = objects[i];
            glm::mat4 model(1.0f);
            model = glm::translate(model, o.position);
            model = glm::rotate(model, o.angles.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, o.angles.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, o.angles.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, o.scale);

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

            // Mando os coeficientes DESTE objeto pro shader. Como cada objeto
            // tem o seu material, isso muda a cada iteração - por isso fica aqui
            // dentro do laço, e não lá fora.
            glUniform1f(kaLoc, o.material.ka);
            glUniform1f(kdLoc, o.material.kd);
            glUniform1f(ksLoc, o.material.ks);
            glUniform1f(qLoc,  o.material.q);

            glBindVertexArray(o.VAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, o.texID);
            glUniform1i(hasTextureLoc, o.hasTexture ? 1 : 0);

            if (showWireframe) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 1.0f);
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUniform1i(wireframeLoc, 0);
            glUniform1f(tintLoc, ((int)i == selected) ? 1.15f : 1.0f);
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

    if (key == GLFW_KEY_G) {
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
