/*
 * Copyright © 2008 Ben Smith
 * Copyright © 2010-2011 Linaro Limited
 *
 * This file is part of the glmark2 OpenGL (ES) 2.0 benchmark.
 *
 * glmark2 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * glmark2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * glmark2.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *  Ben Smith (original glmark benchmark)
 *  Alexandros Frantzis (glmark2)
 */
#include "scene.h"
#include "log.h"
#include "mat.h"
#include "stack.h"
#include "shader-source.h"
#include <cmath>

SceneBuild::SceneBuild(Canvas &pCanvas) :
    Scene(pCanvas, "build")
{
    mOptions["use-vbo"] = Scene::Option("use-vbo", "true",
                                        "Whether to use VBOs for rendering [true,false]");
    mOptions["interleave"] = Scene::Option("interleave", "false",
                                           "Whether to interleave vertex attribute data [true,false]");
    mOptions["model"] = Scene::Option("model", "horse",
                                      "Which model to use [horse, bunny]");
}

SceneBuild::~SceneBuild()
{
}

int SceneBuild::load()
{
    mRotationSpeed = 36.0f;

    mRunning = false;

    return 1;
}

void SceneBuild::unload()
{
    mMesh.reset();

}

void SceneBuild::setup()
{
    using LibMatrix::vec3;

    Scene::setup();

    /* Set up shaders */
    static const std::string vtx_shader_filename(GLMARK_DATA_PATH"/shaders/light-basic.vert");
    static const std::string frg_shader_filename(GLMARK_DATA_PATH"/shaders/light-basic.frag");
    static const LibMatrix::vec4 lightPosition(20.0f, 20.0f, 10.0f, 1.0f);
    static const LibMatrix::vec4 materialDiffuse(1.0f, 1.0f, 1.0f, 1.0f);

    ShaderSource vtx_source(vtx_shader_filename);
    ShaderSource frg_source(frg_shader_filename);

    vtx_source.add_const("LightSourcePosition", lightPosition);
    vtx_source.add_const("MaterialDiffuse", materialDiffuse);

    if (!Scene::load_shaders_from_strings(mProgram, vtx_source.str(),
                                          frg_source.str()))
    {
        return;
    }

    Model model;
    bool modelLoaded(false);
    const std::string& whichModel(mOptions["model"].value);

    if (whichModel == "bunny")
    {
        // Bunny rotates around the Y axis
        modelLoaded = model.load_obj(GLMARK_DATA_PATH"/models/bunny.obj");
    }
    else
    {
        // Default is "horse", so we don't need to look further
        // Horse rotates around the Y axis
        modelLoaded = model.load_3ds(GLMARK_DATA_PATH"/models/horse.3ds");
    }

    if(!modelLoaded)
        return;

    model.calculate_normals();

    /* Tell the converter that we only care about position and normal attributes */
    std::vector<std::pair<Model::AttribType, int> > attribs;
    attribs.push_back(std::pair<Model::AttribType, int>(Model::AttribTypePosition, 3));
    attribs.push_back(std::pair<Model::AttribType, int>(Model::AttribTypeNormal, 3));

    model.convert_to_mesh(mMesh, attribs);

    std::vector<GLint> attrib_locations;
    attrib_locations.push_back(mProgram["position"].location());
    attrib_locations.push_back(mProgram["normal"].location());
    mMesh.set_attrib_locations(attrib_locations);

    mUseVbo = (mOptions["use-vbo"].value == "true");
    bool interleave = (mOptions["interleave"].value == "true");

    mMesh.interleave(interleave);

    if (mUseVbo)
        mMesh.build_vbo();
    else
        mMesh.build_array();

    /* Calculate a projection matrix that is a good fit for the model */
    vec3 maxVec = model.maxVec();
    vec3 minVec = model.minVec();
    vec3 diffVec = maxVec - minVec;
    mCenterVec = maxVec + minVec;
    mCenterVec /= 2.0;
    float diameter = diffVec.length();
    mRadius = diameter / 2;
    float fovy = 2.0 * atanf(mRadius / (2.0 + mRadius));
    fovy /= M_PI;
    fovy *= 180.0;
    float aspect(static_cast<float>(mCanvas.width())/static_cast<float>(mCanvas.height()));
    mPerspective.setIdentity();
    mPerspective *= LibMatrix::Mat4::perspective(fovy, aspect, 2.0, 2.0 + diameter); 

    mProgram.start();

    mCurrentFrame = 0;
    mRotation = 0.0;
    mRunning = true;
    mStartTime = Scene::get_timestamp_us() / 1000000.0;
    mLastUpdateTime = mStartTime;
}

void
SceneBuild::teardown()
{
    mProgram.stop();
    mProgram.release();

    mMesh.reset();

    Scene::teardown();
}

void SceneBuild::update()
{
    double current_time = Scene::get_timestamp_us() / 1000000.0;
    double dt = current_time - mLastUpdateTime;
    double elapsed_time = current_time - mStartTime;

    mLastUpdateTime = current_time;

    if (elapsed_time >= mDuration) {
        mAverageFPS = mCurrentFrame / elapsed_time;
        mRunning = false;
    }

    mRotation += mRotationSpeed * dt;

    mCurrentFrame++;
}

void SceneBuild::draw()
{
    LibMatrix::Stack4 model_view;

    // Load the ModelViewProjectionMatrix uniform in the shader
    LibMatrix::mat4 model_view_proj(mPerspective);
    model_view.translate(-mCenterVec.x(), -mCenterVec.y(), -(mCenterVec.z() + 2.0 + mRadius));
    model_view.rotate(mRotation, 0.0f, 1.0f, 0.0f);
    model_view_proj *= model_view.getCurrent();

    mProgram["ModelViewProjectionMatrix"] = model_view_proj;

    // Load the NormalMatrix uniform in the shader. The NormalMatrix is the
    // inverse transpose of the model view matrix.
    LibMatrix::mat4 normal_matrix(model_view.getCurrent());
    normal_matrix.inverse().transpose();
    mProgram["NormalMatrix"] = normal_matrix;

    if (mUseVbo) {
        mMesh.render_vbo();
    }
    else {
        mMesh.render_array();
    }
}

Scene::ValidationResult
SceneBuild::validate()
{
    static const double radius_3d(std::sqrt(3.0));

    if (mRotation != 0)
        return Scene::ValidationUnknown;

    Canvas::Pixel ref(0xa7, 0xa7, 0xa7, 0xff);
    Canvas::Pixel pixel = mCanvas.read_pixel(mCanvas.width() / 2,
                                             mCanvas.height() / 2);

    double dist = pixel_value_distance(pixel, ref);
    if (dist < radius_3d + 0.01) {
        return Scene::ValidationSuccess;
    }
    else {
        Log::debug("Validation failed! Expected: 0x%x Actual: 0x%x Distance: %f\n",
                    ref.to_le32(), pixel.to_le32(), dist);
        return Scene::ValidationFailure;
    }
}
