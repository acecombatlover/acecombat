//
// open horizon -- undefined_darkness@outlook.com
//

#pragma once

#include "phys/physics.h"
#include "renderer/scene.h"
#include "gui/hud.h"
#include <memory>
#include <list>

namespace game
{
//------------------------------------------------------------

typedef nya_math::vec3 vec3;
typedef nya_math::quat quat;
typedef params::fvalue fvalue;
typedef params::uvalue uvalue;
typedef params::value<int> ivalue;
typedef params::value<bool> bvalue;

class world;

//------------------------------------------------------------

template <typename t> using ptr = std::shared_ptr<t>;
template <typename t> using w_ptr = std::weak_ptr<t>;

//------------------------------------------------------------

struct object
{
};

typedef ptr<object> object_ptr;

//------------------------------------------------------------

struct plane_controls: public phys::plane_controls
{
    bvalue missile;
    bvalue mgun;
    bvalue flares;
    bvalue change_weapon;
    bvalue change_target;
    bvalue change_camera;
};

//------------------------------------------------------------

struct missile: public object
{
    phys::missile_ptr phys;
    renderer::missile_ptr render;
    ivalue time;

    void update(int dt, world &w);
    void release();
};

typedef ptr<missile> missile_ptr;

//------------------------------------------------------------

struct plane: public object
{
    plane_controls controls;
    plane_controls last_controls;
    phys::plane_ptr phys;
    renderer::aircraft_ptr render;
    bvalue special_weapon;
    bvalue need_fire_missile;
    ivalue rocket_bay_time;

    std::string missile_model, missile_id;
    ivalue missile_mount_idx;
    ivalue missile_cooldown[2];
    std::vector<ivalue> missile_mount_cooldown;
    std::string special_model, special_id;
    ivalue special_cooldown[2];
    std::vector<ivalue> special_mount_cooldown;
    ivalue special_mount_idx;

    std::list<w_ptr<plane> > targets_air;

    void set_pos(const vec3 &pos) { if (phys) phys->pos = pos; }
    void set_rot(const quat &rot) { if (phys) phys->rot = rot; }
    const vec3 &get_pos() { if (phys) return phys->pos; static vec3 p; return p; }
    const quat &get_rot() { if (phys) return phys->rot; static quat r; return r; }

    void update(int dt, world &w, gui::hud &h, bool player);
};

typedef ptr<plane> plane_ptr;

//------------------------------------------------------------

class world
{
public:
    void set_location(const char *name);

    plane_ptr add_plane(const char *name, int color, bool player);
    missile_ptr add_missile(const char *model, const char *id);

    int get_planes_count() { return (int)m_planes.size(); }
    plane_ptr get_plane(int idx);

    bool is_ally(const plane_ptr &a, const plane_ptr &b);
    typedef std::function<bool(const plane_ptr &a, const plane_ptr &b)> is_ally_handler;
    void set_ally_handler(is_ally_handler &handler) { m_ally_handler = handler; }

    gui::hud &get_hud() { return m_hud; }

    void update(int dt);

    world(renderer::world &w, gui::hud &h): m_render_world(w), m_hud(h) {}

private:
    void update_plane(plane_ptr &p);

private:
    std::vector<plane_ptr> m_planes;
    std::vector<missile_ptr> m_missiles;
    renderer::world &m_render_world;
    gui::hud &m_hud;
    phys::world m_phys_world;

    is_ally_handler m_ally_handler;
};

//------------------------------------------------------------

class game_mode
{
public:
    virtual void update(int dt, const plane_controls &player_controls) {}

    game_mode(world &w): m_world(w) {}

protected:
    world &m_world;
};
//------------------------------------------------------------
}
