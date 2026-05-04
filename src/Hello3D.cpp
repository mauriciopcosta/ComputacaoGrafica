/* Desafio M2 - Cubos Interativos
 *
 * Adaptado a partir do Hello3D do Módulo 1
 * por Maurício Pereira da Costa - Computação Gráfica (Unisinos)
 *
 * Objetivos:
 *  - Geometria de cubo (6 faces, cada uma com cor diferente)
 *  - Rotação independente por eixo (teclas X, Y, Z)
 *  - Translação via teclado: WASD (eixos X e Z), IJ (eixo Y)
 *  - Escala uniforme: [ diminui, ] aumenta
 *  - Instanciar múltiplos cubos: N adiciona, TAB alterna o selecionado
 */

#include <iostream>
#include <string>
#include <vector>
#include <assert.h>

using namespace std;

// GLAD
#include <glad/glad.h>

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Protótipo da função de callback de teclado
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);

// Protótipos das funções
int setupShader();
int setupGeometry();

// Dimensões da janela (pode ser alterado em tempo de execução)
const GLuint WIDTH = 1000, HEIGHT = 1000;

// Vertex Shader - agora com matrizes view e projection
const GLchar* vertexShaderSource = "#version 450\n"
"layout (location = 0) in vec3 position;\n"
"layout (location = 1) in vec3 color;\n"
"uniform mat4 model;\n"
"uniform mat4 view;\n"
"uniform mat4 projection;\n"
"out vec4 finalColor;\n"
"void main()\n"
"{\n"
"gl_Position = projection * view * model * vec4(position, 1.0);\n"
"finalColor = vec4(color, 1.0);\n"
"}\0";

// Fragment Shader
const GLchar* fragmentShaderSource = "#version 450\n"
"in vec4 finalColor;\n"
"out vec4 color;\n"
"void main()\n"
"{\n"
"color = finalColor;\n"
"}\n\0";

// Estrutura que representa cada instância de cubo na cena
// Cada cubo guarda seu próprio estado de transformação
struct Cube {
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
	float scale = 1.0f;
	// Eixo de rotação ativo: 0 = nenhum, 1 = X, 2 = Y, 3 = Z
	int rotAxis = 0;
	// Ângulo acumulado, para preservar a orientação ao parar/trocar o eixo
	float angle = 0.0f;
};

vector<Cube> cubes;
int selected = 0;

// Velocidades das transformações por segundo
const float MOVE_SPEED = 1.5f;
const float SCALE_SPEED = 1.5f;
const float ROT_SPEED = 1.5f;

int main()
{
	// Inicialização da GLFW
	glfwInit();

	// Criação da janela GLFW
	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Desafio M2 - Cubos Interativos -- Mauricio Pereira da Costa", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	glfwSetKeyCallback(window, key_callback);

	// GLAD: carrega todos os ponteiros das funções da OpenGL
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
	}

	const GLubyte* renderer = glGetString(GL_RENDERER);
	const GLubyte* version = glGetString(GL_VERSION);
	cout << "Renderer: " << renderer << endl;
	cout << "OpenGL version supported " << version << endl;

	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);

	GLuint shaderID = setupShader();
	GLuint VAO = setupGeometry();

	glUseProgram(shaderID);

	GLint modelLoc = glGetUniformLocation(shaderID, "model");
	GLint viewLoc = glGetUniformLocation(shaderID, "view");
	GLint projLoc = glGetUniformLocation(shaderID, "projection");

	// Matriz de view: câmera fixa em (0, 0, 3) olhando para a origem
	glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 3.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

	// Matriz de projeção perspectiva
	glm::mat4 projection = glm::perspective(
		glm::radians(45.0f),
		(float)width / (float)height,
		0.1f, 100.0f
	);
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

	glEnable(GL_DEPTH_TEST);

	// Cubo inicial na cena
	cubes.push_back(Cube());

	float lastTime = (float)glfwGetTime();

	// Loop da aplicação
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float now = (float)glfwGetTime();
		float dt = now - lastTime;
		lastTime = now;

		// Leitura contínua das teclas para translação e escala do cubo selecionado
		if (!cubes.empty())
		{
			Cube& c = cubes[selected];

			// Translação - eixos X e Z (WASD)
			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) c.position.x -= MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) c.position.x += MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) c.position.z -= MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) c.position.z += MOVE_SPEED * dt;

			// Translação - eixo Y (I sobe, J desce)
			if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) c.position.y += MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) c.position.y -= MOVE_SPEED * dt;

			// Escala uniforme - [ diminui, ] aumenta
			if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
				c.scale -= SCALE_SPEED * dt * c.scale;
			if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
				c.scale += SCALE_SPEED * dt * c.scale;
			if (c.scale < 0.05f) c.scale = 0.05f;
		}

		// Limpa os buffers de cor e profundidade
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindVertexArray(VAO);

		// Desenha cada cubo com sua matriz de modelo própria
		for (size_t i = 0; i < cubes.size(); i++)
		{
			Cube& c = cubes[i];

			// Acumula o ângulo apenas se o cubo estiver rotacionando
			if (c.rotAxis != 0) c.angle += ROT_SPEED * dt;

			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, c.position);

			if (c.rotAxis == 1)
				model = glm::rotate(model, c.angle, glm::vec3(1.0f, 0.0f, 0.0f));
			else if (c.rotAxis == 2)
				model = glm::rotate(model, c.angle, glm::vec3(0.0f, 1.0f, 0.0f));
			else if (c.rotAxis == 3)
				model = glm::rotate(model, c.angle, glm::vec3(0.0f, 0.0f, 1.0f));

			model = glm::scale(model, glm::vec3(c.scale));

			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

			glDrawArrays(GL_TRIANGLES, 0, 36);
		}

		glBindVertexArray(0);
		glfwSwapBuffers(window);
	}

	glDeleteVertexArrays(1, &VAO);
	glfwTerminate();
	return 0;
}

// Callback de eventos discretos: rotação, instanciar novo cubo, alternar seleção
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (action != GLFW_PRESS) return;

	if (key == GLFW_KEY_ESCAPE)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (cubes.empty()) return;
	Cube& c = cubes[selected];

	// Alterna o eixo de rotação do cubo selecionado
	// Pressionar a mesma tecla novamente para o cubo
	if (key == GLFW_KEY_X) c.rotAxis = (c.rotAxis == 1) ? 0 : 1;
	if (key == GLFW_KEY_Y) c.rotAxis = (c.rotAxis == 2) ? 0 : 2;
	if (key == GLFW_KEY_Z) c.rotAxis = (c.rotAxis == 3) ? 0 : 3;

	// N - instancia um novo cubo, deslocado para não sobrepor o anterior
	if (key == GLFW_KEY_N)
	{
		Cube novo;
		novo.position = glm::vec3(
			-1.0f + 0.4f * (float)cubes.size(),
			0.0f,
			0.0f
		);
		novo.scale = 0.5f;
		cubes.push_back(novo);
		selected = (int)cubes.size() - 1;
		cout << "Novo cubo instanciado. Total: " << cubes.size()
			 << " | Selecionado: " << selected << endl;
	}

	// TAB - alterna o cubo selecionado (recebe os comandos)
	if (key == GLFW_KEY_TAB)
	{
		selected = (selected + 1) % (int)cubes.size();
		cout << "Cubo selecionado: " << selected << endl;
	}
}

int setupShader()
{
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);
	GLint success;
	GLchar infoLog[512];
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
	}
	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
	}
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return shaderProgram;
}

// Geometria do cubo: 6 faces, cada uma composta por 2 triângulos (6 vértices),
// com uma cor distinta por face para facilitar a identificação visual.
// Layout do VBO (mesmo padrão do esquema do Módulo 1):
//   atributo 0 -> posição (x, y, z)  - 3 floats, offset 0
//   atributo 1 -> cor     (r, g, b)  - 3 floats, offset 12 bytes
//   stride = 6 * sizeof(GLfloat)
int setupGeometry()
{
	GLfloat vertices[] = {
		// Face frontal (+Z) - vermelho
		-0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		-0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,

		// Face traseira (-Z) - verde
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,

		// Face esquerda (-X) - azul
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,

		// Face direita (+X) - amarelo
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.0f,

		// Face superior (+Y) - magenta
		-0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,

		// Face inferior (-Y) - ciano
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
	};

	GLuint VBO, VAO;

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	// Atributo posição (x, y, z) - location 0
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);

	// Atributo cor (r, g, b) - location 1
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return VAO;
}
