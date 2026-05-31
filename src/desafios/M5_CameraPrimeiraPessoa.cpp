/* Desafio Modulo 5 - Camera em primeira pessoa
 *
 * Peguei a cena do M4 (Phong) e troquei o lookAt fixo por uma classe Camera
 * que da pra navegar com WASD + mouse. Autor: Mauricio Pereira da Costa.
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

// Shaders identicos aos do M4: Phong por pixel com uma fonte de luz pontual.
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

const GLchar* fragmentShaderSource = R"(
#version 450
in vec3 vColor;
in vec2 vTexCoord;
in vec3 fragPos;
in vec3 vNormal;

uniform sampler2D tex_buffer;
uniform int   hasTexture;
uniform int   wireframe;

uniform float ka;
uniform float kd;
uniform float ks;
uniform float q;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 cameraPos;
uniform float ambientStrength;

out vec4 color;

void main() {
    if (wireframe == 1) {
        color = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 objectColor = (hasTexture == 1) ? texture(tex_buffer, vTexCoord).rgb : vColor;

    vec3 ambient = ambientStrength * ka * lightColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightPos - fragPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = kd * diff * lightColor;

    // A vista V agora vem da posicao real da camera (cameraPos), que se move
    // pela cena -> o brilho especular acompanha o observador.
    vec3 V = normalize(cameraPos - fragPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), q);
    vec3 specular = ks * spec * lightColor;

    vec3 result = (ambient + diffuse) * objectColor + specular;
    color = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

// Direcoes possiveis de movimento da camera (interpretadas no metodo move).
enum class Direction { Forward, Backward, Left, Right, Up, Down };

// Classe Camera: agrupa posicao e orientacao do observador e encapsula as
// acoes de Mover e Rotacionar. A view eh derivada com lookAt(pos, pos+front, up).
class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;          // rotacao em torno de Y (olhar para os lados)
    float pitch;        // rotacao em torno de X (olhar para cima/baixo)
    float fov;          // campo de visao (usado na projecao / zoom)
    float speed;        // unidades por segundo
    float sensitivity;  // sensibilidade do mouse

    Camera(glm::vec3 startPos = glm::vec3(0.0f, 0.0f, 3.0f))
        : position(startPos),
          front(glm::vec3(0.0f, 0.0f, -1.0f)),
          worldUp(glm::vec3(0.0f, 1.0f, 0.0f)),
          yaw(-90.0f),   // -90 faz o front apontar para -Z no inicio
          pitch(0.0f),
          fov(45.0f),
          speed(3.0f),
          sensitivity(0.1f) {
        updateVectors();
    }

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    // Mover: desloca a camera na direcao pedida, modulando pelo deltaTime.
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

    // Rotacionar: aplica o deslocamento do mouse ao yaw/pitch (ja amortecido
    // pela sensibilidade), trava o pitch para nao "virar de cabeca para baixo"
    // e recalcula a base de vetores da camera.
    void rotate(float xoffset, float yoffset) {
        yaw   += xoffset * sensitivity;
        pitch += yoffset * sensitivity;

        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateVectors();
    }

    // Zoom: o scroll reduz/aumenta o FOV dentro de uma faixa util.
    void zoom(float yoffset) {
        fov -= yoffset;
        if (fov < 1.0f)  fov = 1.0f;
        if (fov > 45.0f) fov = 45.0f;
    }

private:
    // Recalcula front a partir dos angulos de Euler e, em seguida, right e up.
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

struct Material {
    float ka = 0.1f;
    float kd = 0.7f;
    float ks = 0.5f;
    float q  = 32.0f;
};

// Malha carregada uma unica vez (VAO + material + textura).
struct Mesh {
    string name;
    GLuint VAO = 0;
    int    nVertices = 0;
    GLuint texID = 0;
    bool   hasTexture = false;
    Material material;
};

// Instancia da malha posicionada na cena (varias instancias podem apontar
// para a mesma malha; a profundidade entre elas eh o que mostra a perspectiva).
struct Instance {
    int       mesh;
    glm::vec3 position;
    float     scale;
    float     rotY;
};

vector<Mesh>     meshes;
vector<Instance> scene;

bool showWireframe = false;

// Fonte de luz pontual fixa, acima da cena.
glm::vec3 lightPos   = glm::vec3(0.0f, 6.0f, 4.0f);
glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

// Camera global e estado do mouse (o material sugere que estes seriam
// atributos de uma classe Camera; os de mouse ficam de fora por serem
// especificos do tratamento de input desta aplicacao).
Camera camera(glm::vec3(0.0f, 1.5f, 8.0f));
bool  firstMouse = true;
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;

// Le um .obj montando um VBO com 11 floats por vertice (igual ao M4):
// xyz (posicao) + rgb (cor neutra) + st (textura) + xyz (normal).
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

// Le um .mtl: coeficientes de material (Ka/Kd/Ks reduzidos a escalar pela
// media dos canais) e o caminho da textura (map_Kd). Igual ao M4.
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

// Le os WASD/Space/Ctrl/Shift e pede a camera para se mover. Fica no loop
// (e nao na callback de teclas) porque sao acoes continuas enquanto a tecla
// estiver pressionada.
void processInput(GLFWwindow* window, float dt) {
    bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS
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
}

int main() {
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(
        WIDTH, HEIGHT,
        "Desafio M5 - Camera em Primeira Pessoa - Mauricio Pereira da Costa",
        nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // Esconde e prende o cursor no centro -> mouse vira o controle de olhar.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

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

    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
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

    glUniform1i(glGetUniformLocation(shaderID, "tex_buffer"), 0);
    glActiveTexture(GL_TEXTURE0);

    glUniform3fv(lightColorLoc, 1, glm::value_ptr(lightColor));
    glUniform3fv(lightPosLoc,   1, glm::value_ptr(lightPos));
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

    // Carrega cada modelo uma unica vez.
    for (const auto& p : objPaths) {
        Mesh m;
        m.name = p.filename().string();

        string mtllib;
        m.VAO = loadOBJWithNormals(p.string(), m.nVertices, mtllib);
        if (m.VAO == 0) continue;

        if (!mtllib.empty()) {
            fs::path mtlPath = p.parent_path() / mtllib;
            string mapKd;
            parseMTL(mtlPath.string(), m.material, mapKd);
            if (!mapKd.empty()) {
                fs::path texPath = mtlPath.parent_path() / mapKd;
                m.texID = loadTexture(texPath.string());
                m.hasTexture = (m.texID != 0);
            }
        }

        meshes.push_back(m);
        cout << "[" << meshes.size() - 1 << "] " << m.name
             << " (" << m.nVertices << " vertices"
             << (m.hasTexture ? ", com textura" : ", sem textura") << ")" << endl;
    }

    if (meshes.empty()) {
        cerr << "Nenhum modelo carregado com sucesso." << endl;
        return -1;
    }

    // Monta a cena: uma grade de instancias indo em profundidade (-Z), para
    // que andar para frente/tras evidencie a perspectiva (objetos mais longe
    // parecem menores).
    const int   cols = 3, rows = 4;
    const float spacingX = 3.0f, spacingZ = 3.5f;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Instance inst;
            inst.mesh     = (r * cols + c) % (int)meshes.size();
            inst.position = glm::vec3((c - (cols - 1) * 0.5f) * spacingX,
                                      0.0f,
                                      -r * spacingZ);
            inst.scale    = 0.7f;
            inst.rotY     = 0.0f;
            scene.push_back(inst);
        }
    }

    cout << "\nControles: WASD anda | Space/Ctrl sobe-desce | Shift corre | mouse olha | scroll zoom | G wireframe | ESC sai\n";

    float lastTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;

        processInput(window, dt);

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // View e projection vem da camera, recalculadas a cada frame.
        glm::mat4 view = camera.viewMatrix();
        glm::mat4 projection = glm::perspective(
            glm::radians(camera.fov), (float)width / (float)height, 0.1f, 100.0f);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(cameraPosLoc, 1, glm::value_ptr(camera.position));

        for (const Instance& inst : scene) {
            const Mesh& m = meshes[inst.mesh];

            glm::mat4 model(1.0f);
            model = glm::translate(model, inst.position);
            model = glm::rotate(model, glm::radians(inst.rotY), glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(inst.scale));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

            glUniform1f(kaLoc, m.material.ka);
            glUniform1f(kdLoc, m.material.kd);
            glUniform1f(ksLoc, m.material.ks);
            glUniform1f(qLoc,  m.material.q);

            glBindVertexArray(m.VAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.texID);
            glUniform1i(hasTextureLoc, m.hasTexture ? 1 : 0);

            if (showWireframe) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 1.0f);
            }
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

        glfwSwapBuffers(window);
    }

    for (auto& m : meshes) {
        glDeleteVertexArrays(1, &m.VAO);
        if (m.texID) glDeleteTextures(1, &m.texID);
    }
    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* /*window*/, int w, int h) {
    if (h <= 0) return;
    glViewport(0, 0, w, h);
}

// Olhar com o mouse: calcula o deslocamento desde o frame anterior e repassa
// para a camera rotacionar. O yoffset eh invertido porque em tela o Y cresce
// para baixo, mas queremos que subir o mouse olhe para cima.
void mouse_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    camera.rotate(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    camera.zoom((float)yoffset);
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }
    if (key == GLFW_KEY_G) {
        showWireframe = !showWireframe;
        cout << "Wireframe: " << (showWireframe ? "ON" : "OFF") << endl;
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
