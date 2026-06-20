/* Grau B - Cena Final (visualizador de cenas 3D)
 *
 * Autor: Maurício Pereira da Costa - Computação Gráfica (Unisinos)
 *
 * Entrega final: integra os recursos desenvolvidos de M2 a M6 num único
 * visualizador e acrescenta os dois itens exigidos pelo Grau B:
 *
 *   (1) A cena é descrita por um ARQUIVO DE CONFIGURAÇÃO (../assets/cena.cfg):
 *       quais .obj instanciar e suas transformações iniciais, as fontes de luz
 *       (posição, cor, intensidade) e a posição/orientação inicial da câmera
 *       mais o frustum (fov, near, far). O parser está na função loadScene.
 *
 *   (2) A animação de trajetória usa CURVA DE BÉZIER cúbica (no M6 era linear).
 *       Cada objeto pode ter pontos de controle; a curva é amostrada sobre eles
 *       e o objeto a percorre de forma cíclica.
 *
 * Recursos herdados dos módulos anteriores:
 *   - leitura de .obj com posição/textura/normal (11 floats por vértice) - M3/M4
 *   - leitura do .mtl (ka, kd, ks, Ns + textura map_Kd)                   - M3/M4
 *   - iluminação de Phong por pixel, com várias luzes e atenuação         - M4 + vivencial
 *   - câmera sintética navegável (WASD + mouse)                           - M5
 *   - seleção de objeto e transformações geométricas                      - M2/M3/M4
 *
 * --------------------------- CONTROLES -------------------------------------
 *  CÂMERA
 *    W A S D        anda | Space/Ctrl sobe-desce | Shift (segurar) corre
 *    mouse          olha em volta | scroll zoom (FOV)
 *  OBJETO SELECIONADO
 *    TAB            seleciona o próximo objeto (fica realçado)
 *    M              cicla o modo de transformação: TRANSLAÇÃO/ROTAÇÃO/ESCALA
 *    setas          aplicam o modo nos eixos X/Y (e PageUp/PageDown no Z)
 *                   (na ESCALA, seta cima/baixo aumenta/diminui - uniforme)
 *  MATERIAIS / TEXTURA
 *    T              liga/desliga a textura (mostra material puro vs texturizado)
 *    G              liga/desliga wireframe
 *  LUZES (Phong)
 *    1 2 3          ligam/desligam a luz 0 / 1 / 2 (key / fill / back)
 *    5 / 6          diminui / aumenta a intensidade geral das luzes
 *    7 / 8          diminui / aumenta o coeficiente especular (ks)
 *  ANIMAÇÃO (Bézier)
 *    ENTER          play/pause da animação
 *    P              adiciona ponto de controle na posição atual da câmera
 *    BACKSPACE      remove o último ponto | C limpa a trajetória do selecionado
 *    [ / ]          diminui / aumenta a velocidade
 *    B              mostra/esconde a curva e os pontos de controle
 *  ARQUIVO
 *    F2             salva a cena atual em ../assets/cena.cfg
 *    ESC            sai
 * ---------------------------------------------------------------------------
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
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
GLuint setupShader();
GLuint loadTexture(const string& filePath);

const GLuint WIDTH = 1400, HEIGHT = 900;
const int    MAX_LIGHTS = 8;   // limite de luzes suportado pelo shader

// ---------------------------------------------------------------------------
// SHADERS  (Phong por pixel; iguais aos do M4/M5/M6, mas com array de luzes)
// ---------------------------------------------------------------------------

// Vertex shader. Além da posição na tela (projection*view*model), propaga ao
// fragment os dados necessários para a iluminação:
//  - fragPos: posição do vértice no espaço do mundo (model * posição);
//  - vNormal: normal transformada por mat3(transpose(inverse(model))), que
//    corrige a normal sob escala não-uniforme.
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

// Fragment shader: cálculo da iluminação (modelo de Phong) por pixel. Percorre
// as luzes ligadas somando difusa + especular (cada uma com atenuação por
// distância e intensidade); a ambiente entra uma única vez. Fórmula final:
//   cor = (ambiente + difusa) * cor_do_objeto + especular
// O uniform useFlat desenha a curva/pontos com cor chapada (ignora a luz).
const GLchar* fragmentShaderSource = R"(
#version 450
#define MAX_LIGHTS 8
in vec3 vColor;
in vec2 vTexCoord;
in vec3 fragPos;
in vec3 vNormal;

uniform sampler2D tex_buffer;
uniform int   hasTexture;
uniform int   wireframe;
uniform float tint;
uniform int   useFlat;
uniform vec3  flatColor;

// material (vem do .mtl)
uniform float ka;
uniform float kd;
uniform float ks;
uniform float q;
uniform float ambientStrength;
uniform float ksGain;        // multiplicador do especular (teclas 7/8)

// luzes
uniform int   numLights;
uniform vec3  lightPos[MAX_LIGHTS];
uniform vec3  lightColor[MAX_LIGHTS];
uniform float lightIntensity[MAX_LIGHTS];
uniform int   lightOn[MAX_LIGHTS];
uniform float lightGain;     // multiplicador geral de intensidade (teclas 5/6)

// atenuacao Fatt = 1/(Kc + Kl*d + Kq*d^2)
uniform float attConst;
uniform float attLinear;
uniform float attQuad;

uniform vec3  cameraPos;

out vec4 color;

void main() {
    if (useFlat == 1) { color = vec4(flatColor, 1.0); return; }
    if (wireframe == 1) { color = vec4(1.0); return; }

    vec3 objectColor = (hasTexture == 1) ? texture(tex_buffer, vTexCoord).rgb : vColor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos - fragPos);   // direcao para o observador

    vec3 ambient = ambientStrength * ka * lightColor[0];
    vec3 diffuseTotal  = vec3(0.0);
    vec3 specularTotal = vec3(0.0);

    for (int i = 0; i < numLights; ++i) {
        if (lightOn[i] == 0) continue;

        vec3  toLight = lightPos[i] - fragPos;
        float d       = length(toLight);
        vec3  L       = toLight / max(d, 0.0001);   // direcao ate a luz
        float fatt    = 1.0 / (attConst + attLinear * d + attQuad * d * d);
        float inten   = lightIntensity[i] * lightGain;

        // difusa: depende do angulo entre a normal e a direcao da luz
        float diff = max(dot(N, L), 0.0);
        diffuseTotal += fatt * inten * kd * diff * lightColor[i];

        // especular: reflexo da luz comparado com a direcao do observador
        vec3  R = reflect(-L, N);
        float spec = pow(max(dot(R, V), 0.0), q);
        specularTotal += fatt * inten * ks * ksGain * spec * lightColor[i];
    }

    vec3 result = (ambient + diffuseTotal) * objectColor + specularTotal;
    color = vec4(clamp(result * tint, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------------------
// CÂMERA
// ---------------------------------------------------------------------------
enum class Direction { Forward, Backward, Left, Right, Up, Down };

class Camera {
public:
    glm::vec3 position;
    glm::vec3 front, up, right, worldUp;
    float yaw, pitch, fov, speed, sensitivity;

    Camera(glm::vec3 startPos = glm::vec3(0.0f, 0.0f, 3.0f))
        : position(startPos), front(glm::vec3(0,0,-1)), worldUp(glm::vec3(0,1,0)),
          yaw(-90.0f), pitch(0.0f), fov(45.0f), speed(3.0f), sensitivity(0.1f) {
        updateVectors();
    }

    // Ajusta a câmera a partir dos valores lidos do arquivo de configuração.
    void configure(glm::vec3 pos, float yaw_, float pitch_, float fov_) {
        position = pos; yaw = yaw_; pitch = pitch_; fov = fov_;
        updateVectors();
    }

    glm::mat4 viewMatrix() const { return glm::lookAt(position, position + front, up); }

    void move(Direction dir, float dt) {
        float v = speed * dt;
        switch (dir) {
            case Direction::Forward:  position += front   * v; break;
            case Direction::Backward: position -= front   * v; break;
            case Direction::Left:     position -= right   * v; break;
            case Direction::Right:    position += right   * v; break;
            case Direction::Up:       position += worldUp * v; break;
            case Direction::Down:     position -= worldUp * v; break;
        }
    }

    void rotate(float xoffset, float yoffset) {
        yaw   += xoffset * sensitivity;
        pitch += yoffset * sensitivity;
        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        updateVectors();
    }

    void zoom(float yoffset) {
        fov -= yoffset;
        if (fov < 1.0f)  fov = 1.0f;
        if (fov > 60.0f) fov = 60.0f;
    }

private:
    void updateVectors() {
        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
        right = glm::normalize(glm::cross(front, worldUp));
        up    = glm::normalize(glm::cross(right, front));
    }
};

// ---------------------------------------------------------------------------
// ESTRUTURAS DE DADOS
// ---------------------------------------------------------------------------
struct Material {
    float ka = 0.1f;
    float kd = 0.7f;
    float ks = 0.5f;
    float q  = 32.0f;
};

// Malha (geometria) carregada uma única vez; vários objetos podem reutilizá-la.
struct Mesh {
    string name;          // nome do arquivo .obj (usado para evitar recarga)
    GLuint VAO = 0;
    int    nVertices = 0;
    GLuint texID = 0;
    bool   hasTexture = false;
    Material material;
};

// Objeto da cena: referencia uma malha e guarda suas transformações e trajetória.
struct Object {
    int       mesh;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);   // graus, por eixo
    float     scale    = 1.0f;              // escala UNIFORME

    vector<glm::vec3> ctrl;    // pontos de controle da Bézier
    vector<glm::vec3> curve;   // curva já amostrada (usada para desenhar e animar)
    int   segIdx  = 0;         // trecho atual da curva amostrada
    float segDist = 0.0f;      // distância já percorrida nesse trecho
};

// Uma fonte de luz da cena.
struct Light {
    glm::vec3 position  = glm::vec3(0.0f);
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 1.0f;
    int       on        = 1;
};

vector<Mesh>   meshes;
vector<Object> scene;
vector<Light>  lights;

int  selected = 0;
bool playing = false;
bool showCurve = true;
bool useTexture = true;     // T liga/desliga a textura globalmente
bool showWireframe = false;

float trajSpeed = 2.5f;
float lightGain = 1.0f;     // multiplicador geral das luzes (5/6)
float ksGain    = 1.0f;     // multiplicador do especular (7/8)

// Modo de transformação do objeto selecionado (alternado pela tecla M).
enum class Mode { Translate, Rotate, Scale };
Mode mode = Mode::Translate;

const float MOVE_SPEED  = 2.0f;
const float ROT_SPEED   = 60.0f;   // graus por segundo
const float SCALE_SPEED = 1.5f;

// Atenuação das luzes (constantes dos slides do M4).
float attConst = 1.0f, attLinear = 0.07f, attQuad = 0.017f;

// Câmera (sobrescrita pela config). O near/far do frustum também vem da config.
Camera camera(glm::vec3(0.0f, 2.0f, 10.0f));
float gNear = 0.1f, gFar = 100.0f;

bool  firstMouse = true;
float lastX = WIDTH / 2.0f, lastY = HEIGHT / 2.0f;

GLuint gLineVAO = 0, gLineVBO = 0;            // buffers reaproveitados para a curva
GLint  gModelLoc = -1, gUseFlatLoc = -1, gFlatColorLoc = -1;

const string modelsDir = "../assets/Modelos3D";
const string sceneFile = "../assets/cena.cfg";

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::Translate: return "TRANSLACAO";
        case Mode::Rotate:    return "ROTACAO";
        case Mode::Scale:     return "ESCALA (uniforme)";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// LEITURA DE .OBJ e .MTL  (igual ao M4/M5/M6)
// ---------------------------------------------------------------------------

// Lê um .obj montando um VBO com 11 floats por vértice:
// xyz (posição) + rgb (cor neutra) + st (textura) + xyz (normal).
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
        string word; ss >> word;

        if (word == "mtllib") {
            ss >> mtllibOut;
        } else if (word == "v") {
            glm::vec3 v; ss >> v.x >> v.y >> v.z; vertices.push_back(v);
        } else if (word == "vt") {
            glm::vec2 vt; ss >> vt.s >> vt.t; texCoords.push_back(vt);
        } else if (word == "vn") {
            glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z; normals.push_back(vn);
        } else if (word == "f") {
            // cada elemento é "v/vt/vn"; separa no '/' e usa os 3 índices (base 1 -> -1)
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

                vBuffer.push_back(vertices[vi].x); vBuffer.push_back(vertices[vi].y); vBuffer.push_back(vertices[vi].z);
                vBuffer.push_back(baseColor.r);    vBuffer.push_back(baseColor.g);    vBuffer.push_back(baseColor.b);
                vBuffer.push_back(vt.s);           vBuffer.push_back(vt.t);
                vBuffer.push_back(vn.x);           vBuffer.push_back(vn.y);           vBuffer.push_back(vn.z);
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
    // descreve, no VAO, como ler cada atributo do vértice (stride de 11 floats)
    const GLsizei stride = 11 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);                     glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(8 * sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = (int)(vBuffer.size() / 11);
    return VAO;
}

// Parser do .mtl: ka/kd/ks (média dos 3 canais reduzida a escalar) + Ns + textura.
void parseMTL(const string& mtlPath, Material& matOut, string& diffuseMapOut) {
    diffuseMapOut.clear();
    ifstream arq(mtlPath.c_str());
    if (!arq.is_open()) return;

    auto avg3 = [](istringstream& ss) {
        float r = 0, g = 0, b = 0; ss >> r >> g >> b; return (r + g + b) / 3.0f;
    };

    string line;
    while (getline(arq, line)) {
        istringstream ss(line);
        string word; ss >> word;
        if (word == "Ka")          matOut.ka = avg3(ss);
        else if (word == "Kd")     matOut.kd = avg3(ss);
        else if (word == "Ks")     matOut.ks = avg3(ss);
        else if (word == "Ns")     ss >> matOut.q;
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
    stbi_set_flip_vertically_on_load(true);   // .obj e stb usam origens opostas no eixo vertical
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 0);
    if (data) {
        GLenum fmt = (nrChannels == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura: " << filePath << " (" << width << "x" << height << ")" << endl;
    } else {
        cerr << "Falha na textura: " << filePath << endl;
        glDeleteTextures(1, &texID);
        texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// Carrega a malha uma única vez: se o .obj já foi carregado, devolve o índice
// existente (permite instanciar o mesmo modelo várias vezes na configuração).
int getOrLoadMesh(const string& objName) {
    for (size_t i = 0; i < meshes.size(); ++i)
        if (meshes[i].name == objName) return (int)i;

    fs::path objPath = fs::path(modelsDir) / objName;
    Mesh m;
    m.name = objName;
    string mtllib;
    m.VAO = loadOBJWithNormals(objPath.string(), m.nVertices, mtllib);
    if (m.VAO == 0) return -1;

    if (!mtllib.empty()) {
        fs::path mtlPath = objPath.parent_path() / mtllib;
        string mapKd;
        parseMTL(mtlPath.string(), m.material, mapKd);
        if (!mapKd.empty()) {
            fs::path texPath = mtlPath.parent_path() / mapKd;
            m.texID = loadTexture(texPath.string());
            m.hasTexture = (m.texID != 0);
        }
    }
    meshes.push_back(m);
    cout << "Malha [" << meshes.size() - 1 << "] " << m.name
         << " (" << m.nVertices << " vert, ka=" << m.material.ka
         << " kd=" << m.material.kd << " ks=" << m.material.ks
         << " q=" << m.material.q << (m.hasTexture ? ", textura" : ", sem textura") << ")" << endl;
    return (int)meshes.size() - 1;
}

// ---------------------------------------------------------------------------
// CURVA DE BÉZIER
// ---------------------------------------------------------------------------

// Avalia uma Bézier cúbica no parâmetro t (forma de Bernstein). Equivale a
// aplicar a matriz de Bézier sobre [P0 P1 P2 P3], aqui na forma aberta com os
// pesos (1-t)^3, 3(1-t)^2 t, 3(1-t) t^2, t^3.
glm::vec3 bezierPoint(const glm::vec3& p0, const glm::vec3& p1,
                      const glm::vec3& p2, const glm::vec3& p3, float t) {
    float u  = 1.0f - t;
    float w0 = u * u * u;
    float w1 = 3.0f * u * u * t;
    float w2 = 3.0f * u * t * t;
    float w3 = t * t * t;
    return w0 * p0 + w1 * p1 + w2 * p2 + w3 * p3;
}

// Reconstrói a curva amostrada a partir dos pontos de controle. Regra: cada 4
// pontos formam um segmento cúbico, avançando de 3 em 3 (o último ponto de um
// segmento é o primeiro do próximo) - por isso usam-se 4, 7, 10... pontos. Com
// menos de 4 pontos, eles são apenas ligados em linha reta.
void rebuildCurve(Object& o) {
    o.curve.clear();
    o.segIdx = 0;
    o.segDist = 0.0f;

    int n = (int)o.ctrl.size();
    if (n < 4) { o.curve = o.ctrl; return; }

    const int SAMPLES = 24;     // amostras por segmento (maior = curva mais lisa)
    int lastEnd = 0;
    for (int i = 0; i + 3 < n; i += 3) {
        // amostra de 0 até SAMPLES-1 para não repetir o ponto que une os segmentos
        for (int s = 0; s < SAMPLES; ++s) {
            float t = (float)s / (float)SAMPLES;
            o.curve.push_back(bezierPoint(o.ctrl[i], o.ctrl[i+1], o.ctrl[i+2], o.ctrl[i+3], t));
        }
        lastEnd = i + 3;
    }
    o.curve.push_back(o.ctrl[lastEnd]);          // fecha o último segmento (t = 1)
    for (int j = lastEnd + 1; j < n; ++j)        // pontos remanescentes: ligados direto
        o.curve.push_back(o.ctrl[j]);
}

// ---------------------------------------------------------------------------
// ANIMAÇÃO  (percorre a curva amostrada com velocidade constante, cíclica)
// Mesma lógica do M6: avança uma distância por frame e a distribui pelos trechos.
// ---------------------------------------------------------------------------
void startPlayback() {
    for (auto& o : scene) {
        o.segIdx = 0; o.segDist = 0.0f;
        if (!o.curve.empty()) o.position = o.curve[0];
    }
}

void updatePlayback(float dt) {
    if (!playing) return;
    for (auto& o : scene) {
        int n = (int)o.curve.size();
        if (n < 2) continue;

        float remaining = trajSpeed * dt;
        int guard = 0;
        while (remaining > 0.0f && guard++ < 20000) {
            glm::vec3 a = o.curve[o.segIdx];
            glm::vec3 b = o.curve[(o.segIdx + 1) % n];
            float segLen = glm::length(b - a);
            if (segLen < 1e-6f) { o.segIdx = (o.segIdx + 1) % n; o.segDist = 0.0f; continue; }

            float left = segLen - o.segDist;
            if (remaining < left) { o.segDist += remaining; remaining = 0.0f; }
            else { remaining -= left; o.segIdx = (o.segIdx + 1) % n; o.segDist = 0.0f; }
        }

        glm::vec3 a = o.curve[o.segIdx];
        glm::vec3 b = o.curve[(o.segIdx + 1) % n];
        float segLen = glm::length(b - a);
        float f = (segLen > 1e-6f) ? (o.segDist / segLen) : 0.0f;
        o.position = a + (b - a) * f;     // interpolação linear dentro do trecho
    }
}

// ---------------------------------------------------------------------------
// PARSER DO ARQUIVO DE CONFIGURAÇÃO DA CENA
// ---------------------------------------------------------------------------
// Lê ../assets/cena.cfg linha a linha. A primeira palavra define o tipo de
// registro; linhas vazias ou iniciadas por '#' são ignoradas. Palavras-chave:
//   camera  px py pz  yaw pitch  fov near far
//   light   px py pz  r g b  intensidade  on
//   object  arquivo.obj  px py pz  rx ry rz  escala
//   bezier  px py pz        -> ponto de controle do ÚLTIMO objeto declarado
bool loadScene(const string& path) {
    ifstream f(path.c_str());
    if (!f.is_open()) { cerr << "Config nao encontrada: " << path << endl; return false; }

    string line;
    while (getline(f, line)) {
        istringstream ss(line);
        string tag;
        if (!(ss >> tag)) continue;                  // linha vazia
        if (tag.empty() || tag[0] == '#') continue;  // comentário

        if (tag == "camera") {
            glm::vec3 p; float yaw, pitch, fov, nearP, farP;
            ss >> p.x >> p.y >> p.z >> yaw >> pitch >> fov >> nearP >> farP;
            camera.configure(p, yaw, pitch, fov);
            gNear = nearP; gFar = farP;
        }
        else if (tag == "light") {
            Light lt;
            ss >> lt.position.x >> lt.position.y >> lt.position.z
               >> lt.color.x >> lt.color.y >> lt.color.z
               >> lt.intensity >> lt.on;
            if ((int)lights.size() < MAX_LIGHTS) lights.push_back(lt);
        }
        else if (tag == "object") {
            string objName;
            Object o;
            ss >> objName
               >> o.position.x >> o.position.y >> o.position.z
               >> o.rotation.x >> o.rotation.y >> o.rotation.z
               >> o.scale;
            int mi = getOrLoadMesh(objName);
            if (mi < 0) { cerr << "Objeto ignorado (falha na malha): " << objName << endl; continue; }
            o.mesh = mi;
            scene.push_back(o);
        }
        else if (tag == "bezier") {
            // anexa o ponto de controle ao último objeto declarado
            if (scene.empty()) continue;
            glm::vec3 p; ss >> p.x >> p.y >> p.z;
            scene.back().ctrl.push_back(p);
        }
    }
    f.close();

    // após a leitura, gera a curva amostrada de cada objeto
    for (auto& o : scene) rebuildCurve(o);

    cout << "Cena carregada: " << scene.size() << " objeto(s), "
         << lights.size() << " luz(es)." << endl;
    return true;
}

// Salva a cena atual no mesmo formato (F2). Permite ajustar a cena em tempo de
// execução e persistir o resultado para a próxima abertura.
void saveScene(const string& path) {
    ofstream f(path.c_str());
    if (!f.is_open()) { cerr << "Falha ao salvar: " << path << endl; return; }

    f << "# Cena salva pelo visualizador (F2)\n";
    f << "camera " << camera.position.x << " " << camera.position.y << " " << camera.position.z
      << "  " << camera.yaw << " " << camera.pitch << "  " << camera.fov
      << " " << gNear << " " << gFar << "\n\n";

    for (const auto& lt : lights)
        f << "light " << lt.position.x << " " << lt.position.y << " " << lt.position.z
          << "  " << lt.color.x << " " << lt.color.y << " " << lt.color.z
          << "  " << lt.intensity << " " << lt.on << "\n";
    f << "\n";

    for (const auto& o : scene) {
        f << "object " << meshes[o.mesh].name
          << "  " << o.position.x << " " << o.position.y << " " << o.position.z
          << "  " << o.rotation.x << " " << o.rotation.y << " " << o.rotation.z
          << "  " << o.scale << "\n";
        for (const auto& p : o.ctrl)
            f << "bezier " << p.x << " " << p.y << " " << p.z << "\n";
    }
    cout << "Cena salva em " << path << endl;
}

// ---------------------------------------------------------------------------
// DESENHO DA CURVA  (reaproveita o shader, com cor chapada via useFlat)
// ---------------------------------------------------------------------------
void setupLineBuffers() {
    glGenVertexArrays(1, &gLineVAO);
    glGenBuffers(1, &gLineVBO);
    glBindVertexArray(gLineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gLineVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void drawPoints(const vector<glm::vec3>& pts, const glm::vec3& colr, GLenum primitive, float size) {
    if (pts.empty()) return;
    glBindVertexArray(gLineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gLineVBO);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(glm::vec3), pts.data(), GL_DYNAMIC_DRAW);

    glm::mat4 id(1.0f);
    glUniformMatrix4fv(gModelLoc, 1, GL_FALSE, glm::value_ptr(id));
    glUniform1i(gUseFlatLoc, 1);
    glUniform3fv(gFlatColorLoc, 1, glm::value_ptr(colr));

    if (primitive == GL_POINTS) glPointSize(size); else glLineWidth(size);
    glDrawArrays(primitive, 0, (GLsizei)pts.size());

    glUniform1i(gUseFlatLoc, 0);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// ENTRADA CONTÍNUA  (câmera + transformação do objeto selecionado)
// ---------------------------------------------------------------------------
void processInput(GLFWwindow* window, float dt) {
    // --- câmera (WASD + Space/Ctrl, Shift corre) ---
    bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
               || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    float saved = camera.speed;
    if (sprint) camera.speed *= 2.5f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.move(Direction::Forward,  dt);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.move(Direction::Backward, dt);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.move(Direction::Left,     dt);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.move(Direction::Right,    dt);
    if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) camera.move(Direction::Up,   dt);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.move(Direction::Down, dt);
    camera.speed = saved;

    if (scene.empty()) return;
    Object& o = scene[selected];

    // --- transformação do objeto selecionado (setas + PageUp/PageDown); a ação
    //     de cada tecla depende do modo atual (alternado pela tecla M) ---
    if (mode == Mode::Translate) {
        if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.position.x -= MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.position.x += MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) o.position.y += MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) o.position.y -= MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) o.position.z -= MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) o.position.z += MOVE_SPEED * dt;
    }
    else if (mode == Mode::Rotate) {
        if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.rotation.y -= ROT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.rotation.y += ROT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) o.rotation.x -= ROT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) o.rotation.x += ROT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) o.rotation.z -= ROT_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) o.rotation.z += ROT_SPEED * dt;
    }
    else { // Escala (uniforme): cima aumenta, baixo diminui
        if (glfwGetKey(window, GLFW_KEY_UP)   == GLFW_PRESS) o.scale *= (1.0f + SCALE_SPEED * dt);
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) o.scale *= (1.0f - SCALE_SPEED * dt);
        if (o.scale < 0.05f) o.scale = 0.05f;
    }
}

// ---------------------------------------------------------------------------
int main() {
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(
        WIDTH, HEIGHT, "Grau B - Cena Final - Mauricio Pereira da Costa", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl; return -1;
    }
    cout << "Renderer: " << glGetString(GL_RENDERER) << "\nOpenGL:   " << glGetString(GL_VERSION) << endl;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // Obtém a location de cada uniform uma vez (ligação entre o C++ e o shader).
    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
    GLint wireframeLoc  = glGetUniformLocation(shaderID, "wireframe");
    GLint hasTextureLoc = glGetUniformLocation(shaderID, "hasTexture");
    GLint tintLoc       = glGetUniformLocation(shaderID, "tint");
    GLint useFlatLoc    = glGetUniformLocation(shaderID, "useFlat");
    GLint flatColorLoc  = glGetUniformLocation(shaderID, "flatColor");
    GLint kaLoc         = glGetUniformLocation(shaderID, "ka");
    GLint kdLoc         = glGetUniformLocation(shaderID, "kd");
    GLint ksLoc         = glGetUniformLocation(shaderID, "ks");
    GLint qLoc          = glGetUniformLocation(shaderID, "q");
    GLint ambStrLoc     = glGetUniformLocation(shaderID, "ambientStrength");
    GLint ksGainLoc     = glGetUniformLocation(shaderID, "ksGain");
    GLint numLightsLoc  = glGetUniformLocation(shaderID, "numLights");
    GLint lightPosLoc   = glGetUniformLocation(shaderID, "lightPos");
    GLint lightColorLoc = glGetUniformLocation(shaderID, "lightColor");
    GLint lightIntLoc   = glGetUniformLocation(shaderID, "lightIntensity");
    GLint lightOnLoc    = glGetUniformLocation(shaderID, "lightOn");
    GLint lightGainLoc  = glGetUniformLocation(shaderID, "lightGain");
    GLint attConstLoc   = glGetUniformLocation(shaderID, "attConst");
    GLint attLinearLoc  = glGetUniformLocation(shaderID, "attLinear");
    GLint attQuadLoc    = glGetUniformLocation(shaderID, "attQuad");
    GLint cameraPosLoc  = glGetUniformLocation(shaderID, "cameraPos");

    gModelLoc = modelLoc; gUseFlatLoc = useFlatLoc; gFlatColorLoc = flatColorLoc;

    glUniform1i(glGetUniformLocation(shaderID, "tex_buffer"), 0);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(ambStrLoc, 0.2f);
    glUniform1f(attConstLoc, attConst);
    glUniform1f(attLinearLoc, attLinear);
    glUniform1f(attQuadLoc, attQuad);
    glUniform1i(useFlatLoc, 0);

    glEnable(GL_DEPTH_TEST);
    setupLineBuffers();

    // === a cena é construída a partir do arquivo de configuração ===
    if (!fs::exists(modelsDir)) {
        cerr << "Pasta nao encontrada: " << modelsDir << " (execute a partir de build/)" << endl;
        return -1;
    }
    if (!loadScene(sceneFile) || scene.empty()) {
        cerr << "Cena vazia ou config ausente (" << sceneFile << ")." << endl;
        return -1;
    }

    cout << "\nControles: WASD+mouse camera | TAB seleciona | M modo | setas transformam\n"
            "1/2/3 luzes | 5/6 intensidade | 7/8 especular | T textura | G wireframe\n"
            "ENTER play | P add ponto | BACKSPACE/C trajetoria | [ ] velocidade | B curva | F2 salva\n";

    float lastTime = (float)glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        float now = (float)glfwGetTime();
        float dt = now - lastTime;
        lastTime = now;

        processInput(window, dt);
        updatePlayback(dt);

        glClearColor(0.06f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // view e projection saem da câmera, recalculadas a cada frame. O frustum
        // (near/far) e o fov vêm da config / do zoom.
        glm::mat4 view = camera.viewMatrix();
        glm::mat4 projection = glm::perspective(
            glm::radians(camera.fov), (float)width / (float)height, gNear, gFar);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(cameraPosLoc, 1, glm::value_ptr(camera.position));

        // envia as luzes ao shader (posições/cores/intensidades/quais ligadas)
        int nL = (int)lights.size();
        vector<glm::vec3> lp(nL), lc(nL); vector<float> li(nL); vector<int> lo(nL);
        for (int i = 0; i < nL; ++i) { lp[i]=lights[i].position; lc[i]=lights[i].color; li[i]=lights[i].intensity; lo[i]=lights[i].on; }
        glUniform1i(numLightsLoc, nL);
        if (nL > 0) {
            glUniform3fv(lightPosLoc,   nL, glm::value_ptr(lp[0]));
            glUniform3fv(lightColorLoc, nL, glm::value_ptr(lc[0]));
            glUniform1fv(lightIntLoc,   nL, li.data());
            glUniform1iv(lightOnLoc,    nL, lo.data());
        }
        glUniform1f(lightGainLoc, lightGain);
        glUniform1f(ksGainLoc, ksGain);

        // 1) desenha os objetos da cena
        for (size_t i = 0; i < scene.size(); ++i) {
            const Object& o = scene[i];
            const Mesh& m = meshes[o.mesh];

            // matriz model: translada -> gira (3 eixos) -> escala uniforme
            glm::mat4 model(1.0f);
            model = glm::translate(model, o.position);
            model = glm::rotate(model, glm::radians(o.rotation.x), glm::vec3(1,0,0));
            model = glm::rotate(model, glm::radians(o.rotation.y), glm::vec3(0,1,0));
            model = glm::rotate(model, glm::radians(o.rotation.z), glm::vec3(0,0,1));
            model = glm::scale(model, glm::vec3(o.scale));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

            // material deste objeto (vindo do .mtl) -> uniforms da iluminação
            glUniform1f(kaLoc, m.material.ka);
            glUniform1f(kdLoc, m.material.kd);
            glUniform1f(ksLoc, m.material.ks);
            glUniform1f(qLoc,  m.material.q);
            glUniform1f(tintLoc, ((int)i == selected) ? 1.35f : 1.0f);

            glBindVertexArray(m.VAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.texID);
            // hasTexture só vale se o objeto tem textura E o usuário não desligou no T
            glUniform1i(hasTextureLoc, (m.hasTexture && useTexture) ? 1 : 0);

            if (showWireframe) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f, 1.0f); }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUniform1i(wireframeLoc, 0);
            glDrawArrays(GL_TRIANGLES, 0, m.nVertices);
            if (showWireframe) glDisable(GL_POLYGON_OFFSET_FILL);

            if (showWireframe) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(1.0f);
                glUniform1i(wireframeLoc, 1);
                glDrawArrays(GL_TRIANGLES, 0, m.nVertices);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        glBindVertexArray(0);

        // 2) desenha a curva de Bézier e os pontos de controle por cima (com o
        //    depth test desligado para ficarem sempre visíveis). Selecionado = amarelo.
        if (showCurve) {
            glDisable(GL_DEPTH_TEST);
            for (size_t i = 0; i < scene.size(); ++i) {
                if (scene[i].ctrl.empty()) continue;
                glm::vec3 cCurve = ((int)i == selected) ? glm::vec3(1.0f, 0.85f, 0.1f) : glm::vec3(0.5f, 0.5f, 0.55f);
                glm::vec3 cCtrl  = ((int)i == selected) ? glm::vec3(0.2f, 1.0f, 0.4f)  : glm::vec3(0.4f, 0.4f, 0.45f);
                if (scene[i].curve.size() >= 2) drawPoints(scene[i].curve, cCurve, GL_LINE_STRIP, 2.0f);
                drawPoints(scene[i].ctrl, cCtrl, GL_POINTS, 10.0f);
            }
            glEnable(GL_DEPTH_TEST);
        }

        glfwSwapBuffers(window);
    }

    for (auto& m : meshes) { glDeleteVertexArrays(1, &m.VAO); if (m.texID) glDeleteTextures(1, &m.texID); }
    glDeleteVertexArrays(1, &gLineVAO);
    glDeleteBuffers(1, &gLineVBO);
    glfwTerminate();
    return 0;
}

// ---------------------------------------------------------------------------
// CALLBACKS
// ---------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* /*window*/, int w, int h) {
    if (h <= 0) return;
    glViewport(0, 0, w, h);
}

void mouse_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;   // invertido: em tela o Y cresce para baixo
    lastX = (float)xpos; lastY = (float)ypos;
    camera.rotate(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    camera.zoom((float)yoffset);
}

// Callback de teclas: apenas as ações de toque único (seleção, modos, toggles).
void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) { glfwSetWindowShouldClose(window, GL_TRUE); return; }
    if (key == GLFW_KEY_G) { showWireframe = !showWireframe; cout << "Wireframe: " << (showWireframe?"ON":"OFF") << endl; return; }
    if (key == GLFW_KEY_T) { useTexture = !useTexture; cout << "Textura: " << (useTexture?"ON":"OFF") << endl; return; }
    if (key == GLFW_KEY_B) { showCurve = !showCurve; return; }

    // luzes
    if (key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_3) {
        int i = (key == GLFW_KEY_1) ? 0 : (key == GLFW_KEY_2) ? 1 : 2;
        if (i < (int)lights.size()) { lights[i].on = 1 - lights[i].on; cout << "Luz " << i << ": " << (lights[i].on?"ON":"OFF") << endl; }
        return;
    }
    if (key == GLFW_KEY_5) { lightGain = std::max(0.0f, lightGain - 0.1f); cout << "Intensidade: " << lightGain << endl; return; }
    if (key == GLFW_KEY_6) { lightGain += 0.1f; cout << "Intensidade: " << lightGain << endl; return; }
    if (key == GLFW_KEY_7) { ksGain = std::max(0.0f, ksGain - 0.1f); cout << "Especular (ks): " << ksGain << endl; return; }
    if (key == GLFW_KEY_8) { ksGain += 0.1f; cout << "Especular (ks): " << ksGain << endl; return; }

    if (key == GLFW_KEY_F2) { saveScene(sceneFile); return; }

    if (scene.empty()) return;

    if (key == GLFW_KEY_TAB) {
        selected = (selected + 1) % (int)scene.size();
        cout << "Selecionado: [" << selected << "] " << meshes[scene[selected].mesh].name
             << " (" << scene[selected].ctrl.size() << " pontos de controle)" << endl;
        return;
    }
    if (key == GLFW_KEY_M) {
        mode = (mode == Mode::Translate) ? Mode::Rotate : (mode == Mode::Rotate) ? Mode::Scale : Mode::Translate;
        cout << "Modo: " << modeName(mode) << endl;
        return;
    }

    // animação / trajetória
    if (key == GLFW_KEY_ENTER) {
        playing = !playing;
        if (playing) startPlayback();
        cout << "Animacao: " << (playing?"PLAY":"PAUSE") << endl;
        return;
    }
    if (key == GLFW_KEY_P) {
        scene[selected].ctrl.push_back(camera.position);
        rebuildCurve(scene[selected]);     // recalcula a Bézier com o ponto novo
        cout << "Ponto #" << scene[selected].ctrl.size() << " adicionado." << endl;
        return;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        if (!scene[selected].ctrl.empty()) { scene[selected].ctrl.pop_back(); rebuildCurve(scene[selected]); }
        return;
    }
    if (key == GLFW_KEY_C) {
        scene[selected].ctrl.clear(); rebuildCurve(scene[selected]);
        cout << "Trajetoria limpa." << endl;
        return;
    }
    if (key == GLFW_KEY_LEFT_BRACKET)  { trajSpeed = std::max(0.2f, trajSpeed - 0.5f); cout << "Velocidade: " << trajSpeed << endl; return; }
    if (key == GLFW_KEY_RIGHT_BRACKET) { trajSpeed += 0.5f; cout << "Velocidade: " << trajSpeed << endl; return; }
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
