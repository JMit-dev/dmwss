#include "gl_widget.hpp"
#include <spdlog/spdlog.h>
#include <QFile>

GLWidget::GLWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_shader_program(nullptr)
    , m_texture(nullptr)
    , m_vbo(QOpenGLBuffer::VertexBuffer)
    , m_texture_width(160)
    , m_texture_height(144) {
}

GLWidget::~GLWidget() {
    makeCurrent();
    m_vao.destroy();
    m_vbo.destroy();
    delete m_texture;
    delete m_shader_program;
    doneCurrent();
}

void GLWidget::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);

    SetupShaders();
    SetupQuad();

    // Create texture
    m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_texture->setSize(m_texture_width, m_texture_height);
    m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_texture->allocateStorage();
    m_texture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    spdlog::info("OpenGL initialized successfully");
}

void GLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_shader_program || !m_texture) return;

    m_shader_program->bind();
    m_texture->bind();

    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();

    m_texture->release();
    m_shader_program->release();
}

void GLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void GLWidget::UpdateFramebuffer(const u32* data, int width, int height) {
    if (!m_texture) return;

    if (width != m_texture_width || height != m_texture_height) {
        m_texture_width = width;
        m_texture_height = height;

        delete m_texture;
        m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
        m_texture->setSize(m_texture_width, m_texture_height);
        m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        m_texture->allocateStorage();
        m_texture->setMinificationFilter(QOpenGLTexture::Nearest);
        m_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }

    makeCurrent();
    m_texture->bind();
    m_texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, data);
    m_texture->release();
    doneCurrent();

    update();
}

void GLWidget::SetupQuad() {
    // Fullscreen quad vertices
    // Position (xy) + TexCoord (uv)
    float vertices[] = {
        // Position    TexCoord
        -1.0f,  1.0f,  0.0f, 0.0f,  // Top-left
        -1.0f, -1.0f,  0.0f, 1.0f,  // Bottom-left
         1.0f,  1.0f,  1.0f, 0.0f,  // Top-right
         1.0f, -1.0f,  1.0f, 1.0f   // Bottom-right
    };

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    // TexCoord attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    m_vao.release();
    m_vbo.release();
}

void GLWidget::SetupShaders() {
    m_shader_program = new QOpenGLShaderProgram(this);

    // Load vertex shader
    QFile vert_file("shaders/display.vert");
    if (!vert_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        spdlog::error("Failed to open vertex shader file");
        return;
    }
    QString vert_source = vert_file.readAll();
    vert_file.close();

    // Load fragment shader
    QFile frag_file("shaders/display.frag");
    if (!frag_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        spdlog::error("Failed to open fragment shader file");
        return;
    }
    QString frag_source = frag_file.readAll();
    frag_file.close();

    // Compile shaders
    if (!m_shader_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vert_source)) {
        spdlog::error("Failed to compile vertex shader: {}",
                     m_shader_program->log().toStdString());
        return;
    }

    if (!m_shader_program->addShaderFromSourceCode(QOpenGLShader::Fragment, frag_source)) {
        spdlog::error("Failed to compile fragment shader: {}",
                     m_shader_program->log().toStdString());
        return;
    }

    if (!m_shader_program->link()) {
        spdlog::error("Failed to link shader program: {}",
                     m_shader_program->log().toStdString());
        return;
    }

    // Bind texture uniform
    m_shader_program->bind();
    m_shader_program->setUniformValue("u_screen_texture", 0);
    m_shader_program->release();

    spdlog::info("Shaders loaded successfully");
}
