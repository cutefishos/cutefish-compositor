// Fragment highp is optional in GLSL ES 1.00; as Chromium's
// gles2_cmd_copy_texture_chromium.cc, fall back to mediump without it.
#if defined(GL_FRAGMENT_PRECISION_HIGH) || defined(CUTEFISH_FRAGMENT_HIGHP)
precision highp float;
#else
precision mediump float;
#endif
