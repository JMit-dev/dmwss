#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
#include "../core/types.hpp"

// Uses only the GL calls common to desktop OpenGL and OpenGL ES (the
// QOpenGLFunctions base, not a versioned Core-Profile-only wrapper) so
// the same widget renders on Android/GLES as well as desktop
class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    // How the 160x144 image maps onto the widget
    enum class ScalingMode {
        STRETCH,   // Fill the widget, ignoring aspect ratio
        FIT,       // Largest size that preserves 10:9 aspect ratio
        INTEGER    // Largest whole-number multiple of 160x144
    };

    explicit GLWidget(QWidget* parent = nullptr);
    ~GLWidget() override;

    void UpdateFramebuffer(const u32* data, int width, int height);

    void SetScalingMode(ScalingMode mode) { m_scaling_mode = mode; update(); }
    ScalingMode GetScalingMode() const { return m_scaling_mode; }

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
    ScalingMode m_scaling_mode = ScalingMode::FIT;

    void SetupQuad();
    void SetupShaders();
};
