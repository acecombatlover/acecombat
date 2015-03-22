//
// open horizon -- undefined_darkness@outlook.com
//

#include "GLFW/glfw3.h"

#include "qdf_provider.h"
#include "../dpl.h" //ToDo: dpl_provider.h

#include "aircraft.h"
#include "location.h"
#include "render/screen_quad.h"

#include "render/vbo.h"
#include "render/fbo.h"
#include "render/render.h"
#include "math/vector.h"
#include "math/quaternion.h"
#include "scene/camera.h"
#include "scene/shader.h"
#include "system/system.h"
#include "memory/memory_reader.h"
#include "stdio.h"
#include "shared.h"
#include "resources/file_resources_provider.h"
#include "zlib.h"

#include <math.h>
#include <vector>
#include <assert.h>

#include "render/debug_draw.h"
nya_render::debug_draw test;

//------------------------------------------------------------

class plane_camera
{
public:
    void add_delta_rot(float dx, float dy);
    void reset_delta_rot() { m_drot.x = 0.0f; m_drot.y = 3.14f; }
    void add_delta_pos(float dx, float dy, float dz);

    void set_aspect(float aspect);
    void set_pos(const nya_math::vec3 &pos) { m_pos = pos; update(); }
    void set_rot(const nya_math::quat &rot) { m_rot = rot; update(); }

private:
    void update();

public:
    plane_camera(): m_dpos(0.0f, 3.0f, 12.0f) { m_drot.y = 3.14f; }

private:
    nya_math::vec3 m_drot;
    nya_math::vec3 m_dpos;

    nya_math::vec3 m_pos;
    nya_math::quat m_rot;
};

//------------------------------------------------------------

void plane_camera::add_delta_rot(float dx, float dy)
{
    m_drot.x += dx;
    m_drot.y += dy;
    
    const float max_angle = 3.14f * 2.0f;
    
    if (m_drot.x > max_angle)
        m_drot.x -= max_angle;
    
    if (m_drot.x < -max_angle)
        m_drot.x += max_angle;
    
    if (m_drot.y > max_angle)
        m_drot.y -= max_angle;
    
    if (m_drot.y< -max_angle)
        m_drot.y+=max_angle;
    
    update();
}

//------------------------------------------------------------

void plane_camera::add_delta_pos(float dx, float dy, float dz)
{
    m_dpos.x -= dx;
    m_dpos.y -= dy;
    m_dpos.z -= dz;
    if (m_dpos.z < 5.0f)
        m_dpos.z = 5.0f;

    if (m_dpos.z > 20000.0f)
        m_dpos.z = 20000.0f;

    update();
}

//------------------------------------------------------------

void plane_camera::set_aspect(float aspect)
{
    nya_scene::get_camera().set_proj(55.0, aspect, 1.0, 64000.0);
                      //1300.0);
    update();
}

//------------------------------------------------------------

void plane_camera::update()
{
    nya_math::quat r = m_rot;
    r.v.x = -r.v.x, r.v.y = -r.v.y;

    r = r*nya_math::quat(m_drot.x, m_drot.y, 0.0f);
    //nya_math::vec3 r=m_drot+m_rot;

    //cam->set_rot(r.y*180.0f/3.14f,r.x*180.0f/3.14f,0.0);
    nya_scene::get_camera().set_rot(r);

    //nya_math::quat rot(-r.x,-r.y,0.0f);
    //rot=rot*m_rot;

    //nya_math::vec3 pos=m_pos+rot.rotate(m_dpos);
    r.v.x = -r.v.x, r.v.y = -r.v.y;
    nya_math::vec3 pos = m_pos + r.rotate(m_dpos);

    nya_scene::get_camera().set_pos(pos.x, pos.y, pos.z);
}

//------------------------------------------------------------

class postprocess
{
public:
    void init(const char *map_name)
    {
        m_fxaa_shader.load("shaders/fxaa.nsh");
        m_main_shader.load("shaders/post_process.nsh");

        if (!map_name)
            return;

        m_quad.init();
        std::string curve_name = std::string("Map/tonecurve_") + map_name + ".tcb";
        nya_resources::resource_data *res = nya_resources::get_resources_provider().access(curve_name.c_str());
        if (res)
        {
            assert(res->get_size() == 256 * 3);
            nya_memory::tmp_buffer_scoped buf(res->get_size());
            res->read_all(buf.get_data());
            res->release();
            const char *data = (const char *)buf.get_data();
            for (int i = 0; i < 3; ++i)
            {
                m_color_curve_tex[i].build_texture(data, 256, 1, nya_render::texture::greyscale);
                m_color_curve_tex[i].set_wrap(false, false);
                data += 256;
            }
        }
    }

    void resize(int width, int height)
    {
        m_main_target.init(width, height, true);
        m_add_target.init(width, height, false);
    }

    void begin_render()
    {
        m_main_target.bind();
    }

    void end_render()
    {
        m_main_target.unbind();
    }

    void draw()
    {
        m_add_target.bind();
        m_fxaa_shader.internal().set();
        m_main_target.color.bind(0);
        m_quad.draw();
        m_add_target.unbind();

        m_add_target.color.bind(0);
        m_main_shader.internal().set();

        for (int i = 0; i < 3; ++i)
            m_color_curve_tex[i].bind(i + 1);

        m_quad.draw();

        for (int i = 0; i < 4; ++i)
            nya_render::texture::unbind(i);
    }

private:
    struct target
    {
        void init(int w, int h, bool has_depth)
        {
            release();
            if (w <= 0 || h <= 0)
                return;

            width = w, height = h;

            color.build_texture(0, width, height, nya_render::texture::color_rgb);
            fbo.set_color_target(color);

            if (has_depth)
            {
                depth.build_texture(0, width, height, nya_render::texture::depth32);
                fbo.set_depth_target(depth);
            }
        }

        bool is_valid() { return width > 0 && height > 0; }

        void release()
        {
            if (!is_valid())
                return;

            color.release();
            depth.release();
            fbo.release();

            width = height = 0;
        }

        void bind()
        {
            was_set = true;
            fbo.bind();
            restore_rect = nya_render::get_viewport();
            nya_render::set_viewport(0, 0, width, height);
        }

        void unbind()
        {
            if (!was_set)
                return;

            was_set = false;
            fbo.unbind();
            nya_render::set_viewport(restore_rect);
        }

        nya_render::fbo fbo;
        nya_render::texture color;
        nya_render::texture depth;
        int width;
        int height;
        bool was_set;
        nya_render::rect restore_rect;

        target(): width(0), height(0), was_set(false) {}
    };

    target m_main_target;
    target m_add_target;
    nya_render::screen_quad m_quad;
    nya_render::texture m_color_curve_tex[3];
    nya_scene::shader m_main_shader;
    nya_scene::shader m_fxaa_shader;
};

//------------------------------------------------------------

nya_memory::tmp_buffer_ref load_resource(const char *name)
{
    nya_resources::resource_data *res = nya_resources::get_resources_provider().access(name);
    if (!res)
        return nya_memory::tmp_buffer_ref();

    nya_memory::tmp_buffer_ref buf(res->get_size());
    res->read_all(buf.get_data());
    res->release();

    return buf;
}

//------------------------------------------------------------

void print_data(const nya_memory::memory_reader &const_reader, size_t offset, size_t size, size_t substruct_size = 0, const char *fileName = 0);

//------------------------------------------------------------

class effect_clouds
{
public:
    void load(const char *location_name)
    {
        bool result = read_bdd((std::string("Effect/") + location_name + "/CloudPosition.BDD").c_str(), m_cloud_positions);
        assert(result);

        result = read_bdd((std::string("Effect/") + location_name + "/cloud_" + location_name + ".BDD").c_str(), m_clouds);
        assert(result);

        std::vector<vert> verts;

        for (int i = 1; i <= 4; ++i)
        {
            if(i!=2) continue; //ToDo

            for (char j = 'A'; j <= 'E'; ++j)
            {
                m_obj_levels.resize(m_obj_levels.size() + 1);

                char buf[512];
                sprintf(buf, "Effect/%s/ObjCloud/Level%d_%c.BOC", location_name, i, j);

                nya_memory::tmp_buffer_ref res = load_resource(buf);
                assert(res.get_size() > 0);
                nya_memory::memory_reader reader(res.get_data(), res.get_size());
                //print_data(reader, 0, reader.get_remained(), 0);

                level_header header = reader.read<level_header>();
                auto &l = m_obj_levels.back();
                l.unknown = header.unknown;
                l.offset = (uint32_t)verts.size();
                l.count = header.count * 6;
                verts.resize(l.offset + l.count);
                for (int k = 0; k < header.count; ++k)
                {
                    level_entry e = reader.read<level_entry>();
                    m_obj_levels.back().entries.push_back(e);

                    vert *v = &verts[l.offset + k * 6];

                    v[0].dir = nya_math::vec2( -1.0f, -1.0f );
                    v[1].dir = nya_math::vec2( -1.0f,  1.0f );
                    v[2].dir = nya_math::vec2(  1.0f,  1.0f );
                    v[3].dir = nya_math::vec2( -1.0f, -1.0f );
                    v[4].dir = nya_math::vec2(  1.0f,  1.0f );
                    v[5].dir = nya_math::vec2(  1.0f, -1.0f );

                    for(int t = 0; t < 6; ++t)
                    {
                        v[t].pos = e.pos;
                        v[t].size.x = e.size;// * 2.0f;
                        v[t].size.y = e.size;// * 2.0f;

                        auto tc=v[t].dir * 0.5f;
                        tc.x += 0.5f, tc.y += 0.5f;

                        v[t].tc.x = tc.x * e.tc[2] + e.tc[0];
                        v[t].tc.y = tc.y * e.tc[2] + e.tc[1];

                        //v[t].tc.y = 1.0f - v[t].tc.y;
                    }
                }

                //print_data(reader, 0, reader.get_remained(), 0);
                res.free();
            }

            break;
        }

        m_mesh.set_vertex_data(&verts[0], uint32_t(sizeof(verts[0])), uint32_t(verts.size()));
        m_mesh.set_vertices(0, 3);
        m_mesh.set_tc(0, 12, 4);
        m_mesh.set_tc(1, 12+16, 2);

        m_shader.load("shaders/clouds.nsh");
        m_obj_tex = shared::get_texture(shared::load_texture((std::string("Effect/") + location_name + "/ObjCloud.nut").c_str()));
        m_flat_tex = shared::get_texture(shared::load_texture((std::string("Effect/") + location_name + "/FlatCloud.nut").c_str()));

        for (int i = 0; i < m_shader.internal().get_uniforms_count(); ++i)
        {
            auto &name = m_shader.internal().get_uniform(i).name;
            if (name == "pos")
                m_shader_pos = i;
            if (name == "up")
                m_shader_up = i;
            if (name == "right")
                m_shader_right = i;
        }

        m_dist_sort.resize(m_clouds.obj_clouds.size());
    }

    void draw()
    {
        nya_render::set_modelview_matrix(nya_scene::get_camera().get_view_matrix());
        nya_render::depth_test::enable(nya_render::depth_test::less);
        nya_render::zwrite::disable();
        nya_render::cull_face::disable();
        nya_render::blend::enable(nya_render::blend::src_alpha, nya_render::blend::inv_src_alpha);

        m_mesh.bind();
        m_shader.internal().set();
        m_obj_tex.internal().set();

        //auto up=nya_scene::get_camera().get_rot().rotate(nya_math::vec3(0.0f,1.0f,0.0f));
        auto up=nya_math::vec3(0.0,1.0,0.0);
        m_shader.internal().set_uniform_value(m_shader_up, up.x, up.y, up.z, 0.0f);

        auto right = nya_scene::get_camera().get_dir();
        std::swap(right.x, right.z);
        right.y=0.0f;
        right.normalize();
        m_shader.internal().set_uniform_value(m_shader_right, right.x, right.y, right.z, 0.0f);

        for(uint32_t i = 0; i < m_dist_sort.size(); ++i)
        {
            auto cp = nya_scene::get_camera().get_pos();
            auto d = m_clouds.obj_clouds[i].second - nya_math::vec2(cp.x,cp.z);
            m_dist_sort[i].first = d * d;
            m_dist_sort[i].second = i;
        }

        std::sort(m_dist_sort.rbegin(), m_dist_sort.rend());

        for(const auto &d: m_dist_sort)
        {
            const auto &o = m_clouds.obj_clouds[d.second];
            auto &l = m_obj_levels[d.second % m_obj_levels.size()];
            m_shader.internal().set_uniform_value(m_shader_pos, o.second.x, 1500.0f, o.second.y, 0.0f);
            m_mesh.draw(l.offset,l.count);
        }

        nya_render::blend::disable();

        m_obj_tex.internal().unset();
        m_shader.internal().unset();
        m_mesh.unbind();
    }

private:
    nya_scene::shader m_shader;
    int m_shader_pos;
    int m_shader_up;
    int m_shader_right;

    nya_scene::texture m_obj_tex;
    nya_scene::texture m_flat_tex;

    nya_render::vbo m_mesh;

    struct vert
    {
        nya_math::vec3 pos;
        nya_math::vec2 tc;
        nya_math::vec2 size;
        nya_math::vec2 dir;
    };

    nya_render::debug_draw m_test;

private:
    typedef unsigned int uint;
    struct bdd_header
    {
        char sign[4];
        uint unknown;

        float unknown2[2];
        uint type1_count;
        float unknown3[2];
        uint type2_count;
        float unknown4[2];
        uint type3_count;
        float unknown5[3];
        uint type4_count;
        float unknown6[2];
        uint obj_clouds_count;
        float params[20];

        uint zero;
    };

    struct bdd
    {
        bdd_header header; //ToDo

        std::vector<nya_math::vec2> type1_pos;
        std::vector<nya_math::vec2> type2_pos;
        std::vector<nya_math::vec2> type3_pos;
        std::vector<nya_math::vec2> type4_pos;
        std::vector<std::pair<int,nya_math::vec2> > obj_clouds;
    };

private:
    bool read_bdd(const char *name, bdd &bdd_res)
    {
        nya_memory::tmp_buffer_ref res = load_resource(name);
        if(!res.get_size())
            return false;

        nya_memory::memory_reader reader(res.get_data(), res.get_size());

        //print_data(reader, reader.get_offset(), reader.get_remained(), 0);

        bdd_header header = reader.read<bdd_header>();

        assert(header.unknown == '0001');
        assert(header.zero == 0);

        bdd_res.type1_pos.resize(header.type1_count);
        for(auto &p: bdd_res.type1_pos)
            p.x = reader.read<float>(), p.y = reader.read<float>();

        bdd_res.type2_pos.resize(header.type2_count);
        for(auto &p: bdd_res.type2_pos)
            p.x = reader.read<float>(), p.y = reader.read<float>();

        bdd_res.type3_pos.resize(header.type3_count);
        for(auto &p: bdd_res.type3_pos)
            p.x = reader.read<float>(), p.y = reader.read<float>();

        bdd_res.type4_pos.resize(header.type4_count);
        for(auto &p: bdd_res.type4_pos)
            p.x = reader.read<float>(), p.y = reader.read<float>();

        bdd_res.obj_clouds.resize(header.obj_clouds_count);
        for(auto &p: bdd_res.obj_clouds)
            p.first = reader.read<int>(), p.second.x = reader.read<float>(), p.second.y = reader.read<float>();

        assert(reader.get_remained()==0);

        bdd_res.header=header; //ToDo

        res.free();
        return true;
    }

    bdd m_cloud_positions;
    bdd m_clouds;

    struct level_header
    {
        uint count;
        float unknown;
        uint zero;
    };

    struct level_entry
    {
        nya_math::vec3 pos;
        float size;
        float tc[6];
        uint zero;
    };

    struct obj_level
    {
        float unknown;
        std::vector<level_entry> entries;

        uint32_t offset;
        uint32_t count;
    };

    std::vector<obj_level> m_obj_levels;

    std::vector<std::pair<uint32_t,uint32_t> > m_dist_sort;
};

//------------------------------------------------------------

int main(void)
{
    const char *plane_name = "su35"; //f22a su35 b02a pkfa su25 su33 su34  kwmr
    const char *plane_color = "color02"; // = 0;
    const char *location_name = "ms01"; //ms01 ms50 ms10

    //plane_name = "f22a",plane_color=0;

#ifndef _WIN32
    chdir(nya_system::get_app_path());
#endif

    qdf_resources_provider qdfp;
    if (!qdfp.open_archive("datafile.qdf"))
    {
        printf("unable to open datafile.qdf");
        return 0;
    }

    nya_resources::set_resources_provider(&qdfp);

    class target_resource_provider: public nya_resources::resources_provider
    {
        nya_resources::resources_provider &m_provider;

    public:
        target_resource_provider(nya_resources::resources_provider &provider): m_provider(provider) {}

    private:
        nya_resources::resource_data *access(const char *resource_name)
        {
            if (!resource_name)
                return 0;

            const std::string str(resource_name);
            if (m_provider.has(("target/" + str).c_str()))
                return m_provider.access(("target/" + str).c_str());

            if (m_provider.has(("common/" + str).c_str()))
                return m_provider.access(("common/" + str).c_str());

            if (m_provider.has(str.c_str()))
                return m_provider.access(str.c_str());

            static nya_resources::file_resources_provider fprov;
            static bool dont_care = fprov.set_folder(nya_system::get_app_path());

            return fprov.access(resource_name);
        }

        bool has(const char *resource_name)
        {
            if (!resource_name)
                return false;

            const std::string str(resource_name);
            return m_provider.has(("target/" + str).c_str()) || m_provider.has(("common/" + str).c_str()) || m_provider.has(resource_name);
        }
    } trp(nya_resources::get_resources_provider());

    nya_resources::set_resources_provider(&trp);

    if (!glfwInit())
        return -1;

    GLFWwindow *window = glfwCreateWindow(1000, 600, "open horizon", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    for (int i = 0; glfwJoystickPresent(i); ++i)
    {
        const char *name = glfwGetJoystickName(i);

        int axis_count = 0, buttons_count = 0;
        glfwGetJoystickAxes(i, &axis_count);
        glfwGetJoystickButtons(i, &buttons_count);
        printf("joy%d: %s %d axis %d buttons\n", i, name, axis_count, buttons_count);
    }

    glfwMakeContextCurrent(window);

    nya_render::texture::set_default_aniso(2);

    location loc;
    loc.load(location_name);

    aircraft player_plane;
    player_plane.load(plane_name, plane_color);
    player_plane.set_pos(nya_math::vec3(-300, 50, 2000));

    plane_camera camera;
    //camera.add_delta_rot(0.1f,0.0f);
    if (plane_name[0] == 'b')
        camera.add_delta_pos(0.0f, -2.0f, -20.0f);

    effect_clouds clouds;
    clouds.load(location_name);

    postprocess pp;
    pp.init(location_name);

    //nya_render::debug_draw test;
    test.set_point_size(5);
    //test.add_point(nya_math::vec3(0.0, 0.0, 0.0),nya_math::vec4(1.0, 0.0, 0.0, 1.0));
    nya_math::aabb aabb;
//    aabb.delta = nya_math::vec3(0.5, 0.3, 0.2);
  //  test.add_aabb(aabb,nya_math::vec4(0.0, 0.0, 1.0, 1.0));

    double mx,my;
    glfwGetCursorPos(window, &mx, &my);

    int frame_counter = 0;
    int frame_counter_time = 0;
    int fps = 0;

    int screen_width = 0, screen_height = 0;
    unsigned long app_time = nya_system::get_time();
    while (!glfwWindowShouldClose(window))
    {
        unsigned long time = nya_system::get_time();
        const int dt = int(time - app_time);
        frame_counter_time += dt;
        ++frame_counter;
        if (frame_counter_time > 1000)
        {
            fps = frame_counter;
            frame_counter = 0;
            frame_counter_time -= 1000;
        }
        app_time = time;

        static bool paused = false;
        static bool speed10x = false;
        if (!paused)
            player_plane.update(speed10x ? dt * 10 : dt);

        char title[255];
        sprintf(title,"speed: %7d alt: %7d \t fps: %3d  %s", int(player_plane.get_speed()), int(player_plane.get_alt()), fps, paused ? "paused" : "");
        glfwSetWindowTitle(window,title);

        int new_screen_width, new_screen_height;
        glfwGetFramebufferSize(window, &new_screen_width, &new_screen_height);
        if (new_screen_width != screen_width || new_screen_height != screen_height)
        {
            screen_width = new_screen_width, screen_height = new_screen_height;
            pp.resize(screen_width, screen_height);
            camera.set_aspect(screen_height > 0 ? float(screen_width) / screen_height : 1.0f);
            nya_render::set_viewport(0, 0, screen_width, screen_height);
        }

        camera.set_pos(player_plane.get_pos());
        camera.set_rot(player_plane.get_rot());

        nya_render::clear(true, true);

        pp.begin_render();

        //nya_render::set_clear_color(0.69,0.74,0.76,1.0); //fog color
        nya_render::clear(true, true);

        //nya_render::set_color(1,1,1,1);

        loc.draw(dt);
        player_plane.draw();

        clouds.draw();

        pp.end_render();
        pp.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();

        double x, y;
        glfwGetCursorPos(window, &x, &y);
        if (glfwGetMouseButton(window, 0))
            camera.add_delta_rot((y - my) * 0.03, (x - mx) * 0.03);
        //else
          //  camera.reset_delta_rot();

        if (glfwGetMouseButton(window, 1))
            camera.add_delta_pos(0, 0, my - y);

        mx = x; my = y;

        nya_math::vec3 c_rot;
        float c_throttle = 0.0f, c_brake = 0.0f;

        int axis_count = 0, buttons_count = 0;
        const float *axis = glfwGetJoystickAxes(0, &axis_count);
        const unsigned char *buttons = glfwGetJoystickButtons(0, &buttons_count);
        const float joy_dead_zone = 0.1f;
        if (axis_count > 1)
        {
            if (fabsf(axis[0]) > joy_dead_zone) c_rot.z = axis[0];
            if (fabsf(axis[1]) > joy_dead_zone) c_rot.x = axis[1];
        }

        if (axis_count > 3)
        {
            if (fabsf(axis[2]) > joy_dead_zone) camera.add_delta_rot(0.0f, -axis[2] * 0.05f);
            if (fabsf(axis[3]) > joy_dead_zone) camera.add_delta_rot(axis[3] * 0.05f, 0.0f);
        }

        if (buttons_count > 11)
        {
            if (buttons[8]) c_rot.y = -1.0f;
            if (buttons[9]) c_rot.y = 1.0f;
            if (buttons[10]) c_brake = 1.0f;
            if (buttons[11]) c_throttle = 1.0f;

            if (buttons[2]) camera.reset_delta_rot();

            static bool last_btn3 = false;
            if (buttons[3] !=0 && !last_btn3)
                paused = !paused;

            last_btn3 = buttons[3] != 0;
        }

        if (glfwGetKey(window, GLFW_KEY_W)) c_throttle = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S)) c_brake = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A)) c_rot.y = -1.0f;
        if (glfwGetKey(window, GLFW_KEY_D)) c_rot.y = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_UP)) c_rot.x = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_DOWN)) c_rot.x = -1.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT)) c_rot.z = -1.0f;
        if (glfwGetKey(window, GLFW_KEY_RIGHT)) c_rot.z = 1.0f;

        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) player_plane.fire_mgun();

        static bool last_control_rocket = false, last_control_special = false;

        if (glfwGetKey(window, GLFW_KEY_SPACE))
        {
            if (!last_control_rocket)
                player_plane.fire_rocket();
            last_control_rocket = true;
        }
        else
            last_control_rocket = false;

        if (glfwGetKey(window, GLFW_KEY_Q))
        {
            if (!last_control_special)
                player_plane.change_weapon();
            last_control_special = true;
        }
        else
            last_control_special = false;

        //if (glfwGetKey(window, GLFW_KEY_L)) loc.load(location_name);

        static bool last_btn_p = false;
        if (glfwGetKey(window, GLFW_KEY_P) && !last_btn_p)
            paused = !paused;

        last_btn_p = glfwGetKey(window, GLFW_KEY_P);

        speed10x = glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT);

        player_plane.set_controls(c_rot, c_throttle, c_brake);
    }

    glfwTerminate();

    return 0;
}

//------------------------------------------------------------