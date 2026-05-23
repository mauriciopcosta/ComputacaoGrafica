/* Atividade Vivencial - Modulo 4
 *
 * Estende o desafio M4 (Phong) com a tecnica de iluminacao de 3 pontos:
 *   - Luz principal (key)    : a mais intensa, define o tom da cena
 *   - Luz de preenchimento (fill): suaviza as sombras da key, menos intensa
 *   - Luz de fundo (back)    : separa o objeto do fundo, vinda de tras
 *
 * As 3 luzes sao pontuais e posicionadas AUTOMATICAMENTE a partir da posicao
 * e da escala do objeto principal da cena (o objeto selecionado). Quando o
 * objeto se move/escala, as luzes acompanham. Cada luz pode ser ligada/
 * desligada por tecla.
 *
 * Acrescenta tambem um fator de atenuacao por distancia na parcela difusa
 * (e especular): Fatt = 1 / (Kc + Kl*d + Kq*d^2).
 *
 * Autor: Mauricio Pereira da Costa - Computacao Grafica (Unisinos)
 *
 * Controles:
 *   TAB                cicla o objeto principal (as luzes seguem ele)
 *   T / R / S          modos Translacao / Rotacao / Escala
 *   Em Translacao:     setas + PageUp/PageDown
 *   Em Rotacao:        X/Y/Z togglam giro no eixo
 *   Em Escala:         X/Y/Z (Shift inverte) e [ ] (uniforme)
 *   1                  liga/desliga a luz PRINCIPAL (key)
 *   2                  liga/desliga a luz de PREENCHIMENTO (fill)
 *   3                  liga/desliga a luz de FUNDO (back)
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
const int    NUM_LIGHTS = 3;

static GLuint gShaderID = 0;
static GLint  gProjLoc  = -1;

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
    vNormal   = mat3(transpose(inverse(model))) * normal;
}
)";

// Fragment shader: Phong com 3 fontes de luz pontuais. A componente ambiente
// entra uma vez; difusa e especular sao somadas por luz, cada uma ponderada
// pela intensidade da luz e por um fator de atenuacao Fatt(distancia).
const GLchar* fragmentShaderSource = R"(
#version 450
#define NUM_LIGHTS 3
in vec3 vColor;
in vec2 vTexCoord;
in vec3 fragPos;
in vec3 vNormal;

uniform sampler2D tex_buffer;
uniform int   hasTexture;
uniform int   wireframe;
uniform float tint;

uniform float ka;
uniform float kd;
uniform float ks;
uniform float q;
uniform float ambientStrength;

uniform vec3  lightPos[NUM_LIGHTS];
uniform vec3  lightColor[NUM_LIGHTS];
uniform float lightIntensity[NUM_LIGHTS];
uniform int   lightOn[NUM_LIGHTS];

// Constantes de atenuacao Fatt = 1/(Kc + Kl*d + Kq*d^2)
uniform float attConst;
uniform float attLinear;
uniform float attQuad;

uniform vec3  cameraPos;

out vec4 color;

void main() {
    if (wireframe == 1) {
        color = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 objectColor = (hasTexture == 1) ? texture(tex_buffer, vTexCoord).rgb : vColor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos - fragPos);

    // Ambiente: usa a cor da luz principal como referencia da luz de fundo do ambiente
    vec3 ambient = ambientStrength * ka * lightColor[0];

    vec3 diffuseTotal  = vec3(0.0);
    vec3 specularTotal = vec3(0.0);

    for (int i = 0; i < NUM_LIGHTS; ++i) {
        if (lightOn[i] == 0) continue;

        vec3  toLight = lightPos[i] - fragPos;
        float d       = length(toLight);
        vec3  L       = toLight / max(d, 0.0001);

        float fatt = 1.0 / (attConst + attLinear * d + attQuad * d * d);

        // Difusa
        float diff = max(dot(N, L), 0.0);
        diffuseTotal += fatt * lightIntensity[i] * kd * diff * lightColor[i];

        // Especular (Phong)
        vec3  R = reflect(-L, N);
        float spec = pow(max(dot(R, V), 0.0), q);
        specularTotal += fatt * lightIntensity[i] * ks * spec * lightColor[i];
    }

    vec3 result = (ambient + diffuseTotal) * objectColor + specularTotal;
    color = vec4(clamp(result * tint, 0.0, 1.0), 1.0);
}
)";

struct Material {
    float ka = 0.1f;
    float kd = 0.7f;
    float ks = 0.5f;
    float q  = 32.0f;
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

// Estado das 3 luzes (posicao recalculada por frame a partir do objeto principal)
glm::vec3 lightPos[NUM_LIGHTS];
glm::vec3 lightColor[NUM_LIGHTS] = {
    glm::vec3(1.0f, 0.97f, 0.92f),  // key  - branca levemente quente
    glm::vec3(0.85f, 0.9f, 1.0f),   // fill - levemente fria/azulada
    glm::vec3(1.0f, 1.0f, 1.0f)     // back - branca
};
float lightIntensity[NUM_LIGHTS] = { 1.0f, 0.45f, 0.8f }; // key forte, fill suave, back media
int   lightOn[NUM_LIGHTS]        = { 1, 1, 1 };
const char* lightNames[NUM_LIGHTS] = { "PRINCIPAL (key)", "PREENCHIMENTO (fill)", "FUNDO (back)" };

// Atenuacao (constantes definidas pelo usuario - slides do M4)
float attConst  = 1.0f;
float attLinear = 0.09f;
float attQuad   = 0.032f;

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

// Posiciona as 3 luzes em torno do objeto principal seguindo a tecnica de
// 3 pontos. O "raio" base escala com o tamanho do objeto, para que as luzes
// fiquem proporcionalmente afastadas quando ele cresce/diminui.
void updateLights(const OBJ& main) {
    glm::vec3 c = main.position;
    float r = glm::max(glm::max(main.scale.x, main.scale.y), main.scale.z);
    float dist = 3.0f * glm::max(r, 0.2f);

    // Key: frente, a direita e acima da camera/objeto
    lightPos[0] = c + dist * glm::vec3( 1.0f,  0.8f,  1.2f);
    // Fill: frente, do lado oposto e mais baixa
    lightPos[1] = c + dist * glm::vec3(-1.2f,  0.2f,  1.0f);
    // Back: atras e acima, para separar do fundo
    lightPos[2] = c + dist * glm::vec3( 0.0f,  1.2f, -1.5f);
}

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
        "Vivencial M4 - Iluminacao de 3 Pontos - Mauricio Pereira da Costa",
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
    GLint cameraPosLoc  = glGetUniformLocation(shaderID, "cameraPos");
    GLint ambStrLoc     = glGetUniformLocation(shaderID, "ambientStrength");
    GLint attConstLoc   = glGetUniformLocation(shaderID, "attConst");
    GLint attLinearLoc  = glGetUniformLocation(shaderID, "attLinear");
    GLint attQuadLoc    = glGetUniformLocation(shaderID, "attQuad");
    GLint lightPosLoc   = glGetUniformLocation(shaderID, "lightPos");
    GLint lightColorLoc = glGetUniformLocation(shaderID, "lightColor");
    GLint lightIntLoc   = glGetUniformLocation(shaderID, "lightIntensity");
    GLint lightOnLoc    = glGetUniformLocation(shaderID, "lightOn");
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

    glUniform3fv(cameraPosLoc, 1, glm::value_ptr(cameraPos));
    glUniform1f(ambStrLoc, 0.2f);
    glUniform1f(attConstLoc,  attConst);
    glUniform1f(attLinearLoc, attLinear);
    glUniform1f(attQuadLoc,   attQuad);

    // Cor e intensidade das luzes nao mudam em runtime -> envia uma vez
    glUniform3fv(lightColorLoc, NUM_LIGHTS, glm::value_ptr(lightColor[0]));
    glUniform1fv(lightIntLoc,   NUM_LIGHTS, lightIntensity);

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
             << (o.hasTexture ? ", com textura" : ", sem textura") << ")" << endl;
        idx++;
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado com sucesso." << endl;
        return -1;
    }

    cout << "\nControles: TAB objeto principal | T/R/S modos | 1/2/3 ligam luzes | G wireframe | ESC sai\n";
    cout << "Luzes: 1=" << lightNames[0] << " 2=" << lightNames[1] << " 3=" << lightNames[2] << endl;
    cout << "Objeto principal: [" << selected << "] " << objects[selected].name << endl;

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

        // Recalcula as 3 luzes a partir do objeto principal selecionado
        updateLights(objects[selected]);
        glUniform3fv(lightPosLoc, NUM_LIGHTS, glm::value_ptr(lightPos[0]));
        glUniform1iv(lightOnLoc,  NUM_LIGHTS, lightOn);

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
        cout << "Objeto principal: [" << selected << "] " << objects[selected].name << endl;
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

    // Liga/desliga cada uma das 3 luzes
    if (key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_3) {
        int i = (key == GLFW_KEY_1) ? 0 : (key == GLFW_KEY_2) ? 1 : 2;
        lightOn[i] = 1 - lightOn[i];
        cout << "Luz " << lightNames[i] << ": " << (lightOn[i] ? "ON" : "OFF") << endl;
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
