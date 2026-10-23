#pragma once

#include "parser/entities/access.h"
#include "parser/parser.h"

struct round_line_t {
    bool in = false;
    bool died = false;
    int kills = 0;
    int damage = 0;
    int side = 0;
};

struct round_eco_t {
    int equipment = 0;
    int saved = 0;
    int earned = 0;
    int live = 0;
    int kills = 0;
};

enum kill_flag_t {
    kf_head = 1,
    kf_wall = 2,
    kf_smoke = 4,
    kf_blind = 8,
    kf_noscope = 16,
    kf_flash = 32,
    kf_opening = 64,
    kf_trade = 128,
};

struct kill_t {
    int tick = 0;
    int killer = -1;
    int victim = -1;
    int assister = -1;
    int flags = 0;
    std::string weapon;
};

struct bomb_t {
    int tick = 0;
    int slot = -1;
    int kind = 0;
};

struct round_info_t {
    int start = 0;
    int decided = -1;
    int reason = 0;
    std::vector<kill_t> kills;
    std::vector<bomb_t> bomb;
};

struct player_stats_t {
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int damage = 0;
    int hs_kills = 0;
    int wallbang_kills = 0;
    int smoke_kills = 0;
    int rounds = 0;
    int kast = 0;
    int shots = 0;
    int hits = 0;
    int head_hits = 0;
    int entry_kills = 0;
    int entry_deaths = 0;
    int trade_kills = 0;
    int clutch_rounds = 0;
    int clutches = 0;
    int multi[6] = {};
    int utility_damage = 0;
    int enemies_flashed = 0;
    int flash_assists = 0;
    std::vector<round_line_t> lines;
    std::vector<round_eco_t> eco;
};

struct round_t {
    bool in = false;
    bool alive = false;
    bool died = false;
    bool kill = false;
    bool assist = false;
    bool traded = false;
    int side = 0;
    int kills = 0;
    int damage = 0;
    int clutch_vs = 0;
};

struct aim_t {
    int slot = -1;
    bool hit = false;
    bool head = false;
};

struct fall_t {
    int victim = -1;
    int killer = -1;
    int tick = 0;
};

struct stats_t {
    static const int SLOTS = 64;
    field_key_t team = access::key("m_iTeamNum");
    field_key_t alive = access::key("m_bPawnIsAlive");
    field_key_t score = access::key("m_iScore");
    field_key_t utility = access::key("m_pActionTrackingServices.m_matchStats.m_iUtilityDamage");
    field_key_t flashed = access::key("m_pActionTrackingServices.m_matchStats.m_iEnemiesFlashed");
    field_key_t freeze = access::key("m_pGameRules.m_bFreezePeriod");
    field_key_t win = access::key("m_pGameRules.m_iRoundWinStatus");
    field_key_t reason = access::key("m_pGameRules.m_eRoundEndReason");
    field_key_t per_round = access::key("m_pActionTrackingServices.m_perRoundStats");
    field_key_t pr_equipment = access::key("m_pActionTrackingServices.m_perRoundStats.m_iEquipmentValue");
    field_key_t pr_saved = access::key("m_pActionTrackingServices.m_perRoundStats.m_iMoneySaved");
    field_key_t pr_earned = access::key("m_pActionTrackingServices.m_perRoundStats.m_iCashEarned");
    field_key_t pr_live = access::key("m_pActionTrackingServices.m_perRoundStats.m_iLiveTime");
    field_key_t pr_kills = access::key("m_pActionTrackingServices.m_perRoundStats.m_iKills");
    int rules = -1;
    int team_cls = -1;
    int start_score[4] = {};
    int winner = 0;
    bool first_kill = false;
    int clutch_slot[4] = {-1, -1, -1, -1};
    std::vector<int> winners;
    std::vector<round_info_t> infos;
    round_info_t info;
    bool live = false;
    bool in_round = false;
    int decided = -1;
    int freeze_tick = -1;
    player_stats_t players[SLOTS];
    round_t round[SLOTS];
    std::vector<fall_t> deaths;
    std::map<int, int> hp;
    std::map<int, int> owner;
    std::set<std::tuple<int, int, int>> hits;
    std::vector<aim_t> pending;
};

namespace stats {
    void on_event(stats_t& s, parser_t& p, const game_event_t& e);
    void on_frame(stats_t& s, parser_t& p);
    void finish(stats_t& s, parser_t& p);
}
