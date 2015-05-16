//
// open horizon -- undefined_darkness@outlook.com
//

#include "game.h"
#include "math/scalar.h"
#include "util/util.h"
#include "util/xml.h"
#include <algorithm>
#include <time.h>

namespace game
{
//------------------------------------------------------------

class weapon_information
{
public:
    struct weapon
    {
        std::string id;
        std::string model;
        int count;
    };

    struct aircraft_weapons
    {
        weapon missile;
        std::vector<weapon> special;
        weapon flare;
    };

public:
    static weapon_information &get()
    {
        static weapon_information info("weapons.xml", "common/Arms/ArmsParam.txt");
        return info;
    }

    aircraft_weapons *get_aircraft_weapons(const char *name)
    {
        if (!name)
            return 0;

        std::string name_str(name);
        std::transform(name_str.begin(), name_str.end(), name_str.begin(), ::tolower);
        auto a = m_aircrafts.find(name);
        if (a == m_aircrafts.end())
            return 0;

        return &a->second;
    }

private:
    weapon_information(const char *xml_name, const char *params)
    {
        pugi::xml_document doc;
        if (!load_xml(xml_name, doc))
            return;

        pugi::xml_node root = doc.child("weapons");
        if (!root)
            return;

        for (pugi::xml_node ac = root.child("aircraft"); ac; ac = ac.next_sibling("aircraft"))
        {
            aircraft_weapons &a = m_aircrafts[ac.attribute("name").as_string("")];

            for (pugi::xml_node wpn = ac.first_child(); wpn; wpn = wpn.next_sibling())
            {
                weapon w;
                w.id = wpn.attribute("id").as_string("");
                w.model = wpn.attribute("model").as_string("");
                w.count = wpn.attribute("count").as_int(0);

                std::string name(wpn.name() ? wpn.name() : "");
                if (name == "msl")
                    a.missile = w;
                else if (name == "spc")
                    a.special.push_back(w);
                else if (name == "flr")
                    a.flare = w;
            }
        }
    }

    std::map<std::string, aircraft_weapons> m_aircrafts;
};

//------------------------------------------------------------

missile_ptr world::add_missile(const char *model, const char *id)
{
    if (!model || !id)
        return missile_ptr();

    missile_ptr m(new missile());
    m->phys = m_phys_world.add_missile(id);
    m->render = m_render_world.add_missile(model);

    static params::text_params param("Arms/ArmsParam.txt");
    const std::string pref = "." + std::string(id) + ".action.";
    m->time = param.get_float(pref + "endTime") * 1000;

    m_missiles.push_back(m);
    return m;
}

//------------------------------------------------------------

plane_ptr world::add_plane(const char *name, int color, bool player)
{
    if (!name)
        return plane_ptr();

    plane_ptr p(new plane());
    p->phys = m_phys_world.add_plane(name);
    p->render = m_render_world.add_aircraft(name, color, player);

    if (player)
        m_hud.load(name);

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    p->render->set_time(tm_now->tm_sec + tm_now->tm_min * 60 + tm_now->tm_hour * 60 * 60); //ToDo

    auto wi = weapon_information::get().get_aircraft_weapons(name);
    if (wi)
    {
        p->render->load_missile(wi->missile.model.c_str(), m_render_world.get_location_params());
        if (!wi->special.empty())
        {
            p->render->load_special(wi->special[0].model.c_str(), m_render_world.get_location_params());
            p->special_model = wi->special[0].model, p->special_id = wi->special[0].id;
        }

        p->missile_model = wi->missile.model, p->missile_id = wi->missile.id;
    }

    m_planes.push_back(p);
    return p;
}

//------------------------------------------------------------

plane_ptr world::get_plane(int idx)
{
    if (idx < 0 || idx >= (int)m_planes.size())
        return plane_ptr();

    return m_planes[idx];
}

//------------------------------------------------------------

void world::set_location(const char *name)
{
    m_render_world.set_location(name);
}

//------------------------------------------------------------

void world::update(int dt)
{
    m_planes.erase(std::remove_if(m_planes.begin(), m_planes.end(), [](plane_ptr &p){ return p.use_count() <= 1; }), m_planes.end());
    m_missiles.erase(std::remove_if(m_missiles.begin(), m_missiles.end(), [](missile_ptr &m){ return m.use_count() <= 1 && m->time <= 0; }), m_missiles.end());

    for (auto &p: m_planes)
        p->phys->controls = p->controls;

    m_phys_world.update(dt, [](phys::object_ptr &a, phys::object_ptr &b) {});

    for (auto &p: m_planes)
        p->update(dt, *this, m_hud, p->render == m_render_world.get_player_aircraft());

    for (auto &m: m_missiles)
        m->update(dt, *this);

    m_render_world.update(dt);
}

//------------------------------------------------------------

bool world::is_ally(const plane_ptr &a, const plane_ptr &b)
{
    if (!m_ally_handler)
        return false;

    return m_ally_handler(a, b);
}

//------------------------------------------------------------

void plane::update(int dt, world &w, gui::hud &h, bool player)
{
    const int missile_cooldown_time = 3500;
    const int special_cooldown_time = 7000;

    render->set_pos(phys->pos);
    render->set_rot(phys->rot);

    //aircraft animations

    const float meps_to_kmph = 3.6f;
    const float speed = phys->vel.length() * meps_to_kmph;
    const float speed_k = nya_math::max((phys->params.move.speed.speedMax - speed) / phys->params.move.speed.speedMax, 0.1f);

    const float el = nya_math::clamp(-controls.rot.z - controls.rot.x, -1.0f, 1.0f) * speed_k;
    const float er = nya_math::clamp(controls.rot.z - controls.rot.x, -1.0f, 1.0f) * speed_k;
    render->set_elev(el, er);

    const float rl = nya_math::clamp(-controls.rot.y + controls.brake, -1.0f, 1.0f) * speed_k;
    const float rr = nya_math::clamp(-controls.rot.y - controls.brake, -1.0f, 1.0f) * speed_k;
    render->set_rudder(rl, rr, -controls.rot.y);

    render->set_aileron(-controls.rot.z * speed_k, controls.rot.z * speed_k);
    render->set_canard(controls.rot.x * speed_k);
    render->set_brake(controls.brake);
    render->set_flaperon(speed < phys->params.move.speed.speedCruising - 100 ? -1.0 : 1.0);
    render->set_wing_sweep(speed >  phys->params.move.speed.speedCruising + 250 ? 1.0 : -1.0);

    render->set_intake_ramp(phys->thrust_time >= phys->params.move.accel.thrustMinWait ? 1.0 : -1.0);

    //weapons

    if (controls.change_weapon && controls.change_weapon != last_controls.change_weapon)
    {
        if (render->has_special_bay())
        {
            if (!special_weapon && render->is_special_bay_closed())
                special_weapon = true;

            if (special_weapon && render->is_special_bay_opened())
                special_weapon = false;

            render->set_special_bay(special_weapon);
        }
        else
            special_weapon = !special_weapon;

        if (player)
        {
            if (special_weapon)
                h.set_missiles(special_id.c_str(), 1); //ToDo: idx
            else
                h.set_missiles(missile_id.c_str(), 0);
        }
    }

    int count = 1;
    if (!special_id.empty())
    {
        if (special_id[1] == '4')
            count = 4;
        else if (special_id[1] == '6')
            count = 6;
    }

    if (controls.missile && controls.missile != last_controls.missile)
    {
        if (special_weapon)
        {
            if (!render->has_special_bay() || render->is_special_bay_opened())
            {
                if ((special_cooldown[0] <=0 || special_cooldown[1] <= 0) && render->get_special_mount_count() > 0)
                {
                    if (count == 1)
                    {
                        if (special_cooldown[0] <= 0)
                            special_cooldown[0] = special_cooldown_time;
                        else if (special_cooldown[1] <= 0)
                            special_cooldown[1] = special_cooldown_time;
                    }
                    else
                        special_cooldown[0] = special_cooldown[1] = special_cooldown_time;

                    special_mount_cooldown.resize(render->get_special_mount_count());

                    render->update(0);
                    for (int i = 0; i < count; ++i)
                    {
                        auto m = w.add_missile(special_model.c_str(), special_id.c_str());
                        special_mount_idx = ++special_mount_idx % render->get_special_mount_count();
                        special_mount_cooldown[special_mount_idx] = special_cooldown_time;
                        render->set_special_visible(special_mount_idx, false);
                        m->phys->pos = render->get_special_mount_pos(special_mount_idx);
                        m->phys->rot = render->get_special_mount_rot(special_mount_idx);
                        m->phys->vel = phys->vel;
                    }
                }
            }
        }
        else
        {
            rocket_bay_time = 3000;
            render->set_missile_bay(true);
            need_fire_missile = true;
        }
    }

    if (need_fire_missile && render->is_missile_ready())
    {
        need_fire_missile = false;
        missile_mount_cooldown.resize(render->get_missile_mount_count());
        if ((missile_cooldown[0] <=0 || missile_cooldown[1] <= 0) && render->get_missile_mount_count() > 0)
        {
            if (missile_cooldown[0] <= 0)
                missile_cooldown[0] = missile_cooldown_time;
            else if (missile_cooldown[1] <= 0)
                missile_cooldown[1] = missile_cooldown_time;

            render->update(0);
            auto m = w.add_missile(missile_model.c_str(), missile_id.c_str());
            missile_mount_idx = ++missile_mount_idx % render->get_missile_mount_count();
            missile_mount_cooldown[missile_mount_idx] = missile_cooldown_time;
            render->set_missile_visible(missile_mount_idx, false);
            m->phys->pos = render->get_missile_mount_pos(missile_mount_idx);
            m->phys->rot = render->get_missile_mount_rot(missile_mount_idx);
            m->phys->vel = phys->vel;
        }
    }

    if (rocket_bay_time > 0)
    {
        rocket_bay_time -= dt;
        if (rocket_bay_time <= 0)
        {
            render->set_missile_bay(false);
            rocket_bay_time = 0;
        }
    }

    for (auto &m: missile_cooldown) if (m > 0) m -= dt;
    for (auto &s: special_cooldown) if (s > 0) s -= dt;

    for (int i = 0; i < (int)missile_mount_cooldown.size(); ++i)
    {
        if (missile_mount_cooldown[i] < 0)
            continue;

        missile_mount_cooldown[i] -= dt;
        if (missile_mount_cooldown[i] < 0)
            render->set_missile_visible(i, true);
    }

    for (int i = 0; i < (int)special_mount_cooldown.size(); ++i)
    {
        if (special_mount_cooldown[i] < 0)
            continue;

        special_mount_cooldown[i] -= dt;
        if (special_mount_cooldown[i] < 0)
            render->set_special_visible(i, true);
    }

    if (controls.mgun != last_controls.mgun)
        render->set_mgun_bay(controls.mgun);

    //cockpit animations and hud

    if (player)
    {
        render->set_speed(speed);

        h.set_project_pos(phys->pos + phys->rot.rotate(nya_math::vec3(0.0, 0.0, 1000.0)));
        h.set_speed(speed);
        h.set_alt(phys->pos.y);

        plane_ptr me;
        for (int i = 0; i < w.get_planes_count(); ++i) //ugly
        {
            auto p = w.get_plane(i);
            if (p.operator->() == this)
            {
                me = p;
                break;
            }
        }

        if (controls.change_target && controls.change_target != last_controls.change_target)
        {
            if (targets_air.size() > 1)
            {
                targets_air.push_back(targets_air.front());
                targets_air.pop_front();
            }
        }

        h.clear_targets();
        for (int i = 0; i < w.get_planes_count(); ++i)
        {
            auto p = w.get_plane(i);
            if (me == p)
                continue;

            auto select = gui::hud::select_not;

            if (!w.is_ally(me, p))
            {
                const float dist = (p->get_pos() - me->get_pos()).length();
                auto fp = std::find_if(targets_air.begin(), targets_air.end(), [p](w_ptr<plane> &t){ return p == t.lock(); });
                if (dist < 12000.0f) //ToDo
                {
                    if (fp == targets_air.end())
                        targets_air.push_back(p);
                }
                else
                    targets_air.erase(fp);

                auto first_target = targets_air.begin();
                if (first_target != targets_air.end())
                {
                    if (p == first_target->lock())
                        select = gui::hud::select_current;
                    else if (++first_target != targets_air.end() && p == first_target->lock())
                        select = gui::hud::select_next;
                }
            }

            h.add_target(p->get_pos(), w.is_ally(me, p) ? gui::hud::target_air_ally : gui::hud::target_air, select);
        }

        if (special_weapon)
        {
            if (count == 1)
            {
                h.set_missile_reload(0, 1.0f - float(special_cooldown[0]) / special_cooldown_time);
                h.set_missile_reload(1, 1.0f - float(special_cooldown[1]) / special_cooldown_time);
            }
            else
            {
                float value = 1.0f - float(special_cooldown[0]) / special_cooldown_time;
                for (int i = 0; i < count; ++i)
                    h.set_missile_reload(i, value);
            }
        }
        else
        {
            h.set_missile_reload(0, 1.0f - float(missile_cooldown[0]) / missile_cooldown_time);
            h.set_missile_reload(1, 1.0f - float(missile_cooldown[1]) / missile_cooldown_time);
        }

        if (controls.change_camera && controls.change_camera != last_controls.change_camera)
        {
            switch (render->get_camera_mode())
            {
                case renderer::aircraft::camera_mode_third: render->set_camera_mode(renderer::aircraft::camera_mode_cockpit); break;
                case renderer::aircraft::camera_mode_cockpit: render->set_camera_mode(renderer::aircraft::camera_mode_first); break;
                case renderer::aircraft::camera_mode_first: render->set_camera_mode(renderer::aircraft::camera_mode_third); break;
            }
        }
    }

    last_controls = controls;
}

//------------------------------------------------------------

void missile::update(int dt, world &w)
{
    render->mdl.set_pos(phys->pos);
    render->mdl.set_rot(phys->rot);

    if (time > 0)
        time -= dt;
}

//------------------------------------------------------------
}
