#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
#include "../core/types.hpp"

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit GLWidget(QWidget* parent = nullptr);
    ~GLWidget() override;

    void UpdateFramebuffer(const u32* data, int width, int height);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    QOpenGLShaderProgram* m_shader_program;
    QOpenGLTexture* m_texture;
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;

    int m_texture_width;
    int m_texture_height;

    void SetupQuad();
    void SetupShaders();
};
