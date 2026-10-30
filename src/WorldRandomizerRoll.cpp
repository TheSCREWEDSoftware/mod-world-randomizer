/*
 * mod-world-randomizer: characters tied to their roll from the web roller (web/index.html).
 *
 * The roller's codes (WR5.<base64url bits>, WR4. before) carry every player's race, class, spec and two
 * professions. A player types .randomizer roll <code> <player number> (or picks themself in the addon) and the
 * character is tied to that player's roll, for good: talents can only go into the rolled tree, and trainers won't
 * teach other primary professions. The race and class must match the roll. A GM can untie a character with
 * .randomizer roll clear.
 *
 * The decoder below reads the same bits roller.js writes (packedCode / writePlayer), in the same order.
 */

#include "WorldRandomizer.h"
#include "WorldRandomizerAchievements.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "WorldPacket.h"

#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    // The roller's lists, in its order (roller.js KEYS / PROFESSIONS)
    constexpr std::array<uint8, 6> CATEGORY_SIZES = { 2, 10, 10, 30, 11, 11 }; // faction race class spec prof1 prof2
    constexpr uint8 COUNT_FIELDS = 10;
    constexpr std::array<uint32, 11> PROFESSION_SKILLS = {
        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_HERBALISM, SKILL_INSCRIPTION,
        SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING, SKILL_TAILORING
    };
    // Apprentice rank of each, and the tool it needs (0 = none): Mining Pick, Skinning Knife, Blacksmith Hammer,
    // Arclight Spanner, Jeweler's Kit, Virtuoso Inking Set, Runed Copper Rod
    constexpr std::array<uint32, 11> PROFESSION_APPRENTICE = {
        2259, 2018, 7411, 4036, 2366, 45357, 25229, 2108, 2575, 8613, 3908
    };
    constexpr std::array<uint32, 11> PROFESSION_TOOL = {
        0, 5956, 6218, 6219, 0, 39505, 20815, 0, 2901, 7005, 0
    };
    constexpr std::array<char const*, 11> PROFESSION_NAMES = {
        "Alchemy", "Blacksmithing", "Enchanting", "Engineering", "Herbalism", "Inscription", "Jewelcrafting",
        "Leatherworking", "Mining", "Skinning", "Tailoring"
    };
    // Talent tab order per class (tabpage 0 / 1 / 2), the same order as the roller's specs
    std::array<char const*, 3> SpecNames(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR:      return { "Arms", "Fury", "Protection" };
            case CLASS_PALADIN:      return { "Holy", "Protection", "Retribution" };
            case CLASS_HUNTER:       return { "Beast Mastery", "Marksmanship", "Survival" };
            case CLASS_ROGUE:        return { "Assassination", "Combat", "Subtlety" };
            case CLASS_PRIEST:       return { "Discipline", "Holy", "Shadow" };
            case CLASS_DEATH_KNIGHT: return { "Blood", "Frost", "Unholy" };
            case CLASS_SHAMAN:       return { "Elemental", "Enhancement", "Restoration" };
            case CLASS_MAGE:         return { "Arcane", "Fire", "Frost" };
            case CLASS_WARLOCK:      return { "Affliction", "Demonology", "Destruction" };
            case CLASS_DRUID:        return { "Balance", "Feral", "Restoration" };
            default:                 return { "?", "?", "?" };
        }
    }

    struct RolledPlayer
    {
        std::string name;
        uint8 race = 0;
        uint8 cls = 0;
        int8 spec = -1;           // talent tab page, -1 = not rolled
        std::array<int8, 2> profs = { -1, -1 }; // roller profession index, -1 = not rolled
    };

    class BitReader
    {
    public:
        explicit BitReader(std::string const& base64url)
        {
            static constexpr char const* ALPHABET =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            uint32 buffer = 0;
            uint8 bufferBits = 0;
            for (char c : base64url)
            {
                char const* found = std::strchr(ALPHABET, c);
                if (!found || !c)
                {
                    _ok = false;
                    return;
                }
                buffer = (buffer << 6) | uint32(found - ALPHABET);
                bufferBits += 6;
                if (bufferBits >= 8)
                {
                    bufferBits -= 8;
                    uint8 byte = uint8((buffer >> bufferBits) & 0xFF);
                    for (int8 b = 7; b >= 0; --b)
                        _bits.push_back((byte >> b) & 1);
                }
            }
        }

        uint32 Get(uint8 count)
        {
            uint32 value = 0;
            for (uint8 i = 0; i < count; ++i)
            {
                if (_pos >= _bits.size())
                {
                    _ok = false;
                    return 0;
                }
                value = (value << 1) | _bits[_pos++];
            }
            return value;
        }

        bool Flag() { return Get(1) == 1; }

        uint32 Varint()
        {
            uint32 value = 0;
            uint32 shift = 0;
            bool more = true;
            while (more && _ok)
            {
                value |= Get(4) << shift;
                shift += 4;
                more = Flag();
                if (shift > 28)
                    _ok = false;
            }
            return value;
        }

        std::string Text()
        {
            uint32 length = Get(6);
            std::string text;
            for (uint32 i = 0; i < length && _ok; ++i)
                text += char(Get(8));
            return text;
        }

        bool Ok() const { return _ok; }

    private:
        std::vector<uint8> _bits;
        std::size_t _pos = 0;
        bool _ok = true;
    };

    // Every player of a WR4 / WR5 code (party or character code); empty if it isn't one
    std::vector<RolledPlayer> DecodeCode(std::string code)
    {
        std::vector<RolledPlayer> players;
        if (code.compare(0, 4, "WR4.") != 0 && code.compare(0, 4, "WR5.") != 0)
            return players;
        uint32 version = code[2] == '5' ? 5 : 4;

        BitReader r(code.substr(4));
        bool party = r.Flag();
        r.Get(2); // era
        r.Flag(); // hardcore
        if (r.Flag()) // world seed
        {
            r.Get(32);
            if (!r.Flag())
                for (uint32 n = r.Get(5); n > 0 && r.Ok(); --n)
                    r.Get(14);
            r.Get(2);
            r.Get(3);
        }

        uint32 count = 1;
        if (party)
        {
            if (version == 4)
            {
                r.Get(2); r.Get(3); r.Get(2); r.Get(2); // faction rule, duplicates, profession duplicates, compatible
            }
            else
            {
                r.Get(2);  // faction rule
                r.Get(24); // limits: race, class, spec, profession (6 bits each)
                r.Get(1);  // profession slot rule
                r.Get(2);  // compatible professions
                r.Get(1);  // draft
                r.Get(16); // draft bans per player (4 bits each)
                if (r.Flag())
                {
                    r.Get(32); // bans: faction 2, race 10, class 10, spec 30, profession 11 = 63 bits
                    r.Get(31);
                }
            }
            count = r.Get(6) + 1;
        }

        for (uint32 i = 0; i < count && r.Ok(); ++i)
        {
            RolledPlayer p;
            p.name = r.Flag() ? Acore::StringFormat("Player {}", i + 1) : r.Text();

            if (!r.Flag()) // preferences: read past them
            {
                r.Get(2); r.Get(2); r.Get(2);
                for (uint8 size : CATEGORY_SIZES)
                {
                    if (r.Flag())
                        continue;
                    uint32 mode = r.Get(2);
                    r.Get(size);
                    if (mode == 3)
                        r.Get(5);
                    if (r.Flag())
                        for (uint8 k = 0; k < size && r.Ok(); ++k)
                            r.Varint();
                }
            }

            if (r.Flag())
            {
                p.race = uint8(r.Get(4));
                p.cls = uint8(r.Get(4));
                r.Flag(); // gender
                uint32 spec = r.Get(2);
                p.spec = spec < 3 ? int8(spec) : -1;
                for (int8& prof : p.profs)
                {
                    uint32 value = r.Get(4);
                    prof = value < PROFESSION_SKILLS.size() ? int8(value) : -1;
                }
                if (r.Flag())
                    r.Get(14); // start zone
            }

            for (uint8 k = 0; k < COUNT_FIELDS && r.Ok(); ++k)
                if (r.Flag())
                    r.Varint();

            if (version >= 5 && r.Flag()) // draft bans: race, class, spec, profession
                for (uint8 k = 0; k < 4 && r.Ok(); ++k)
                    for (uint32 n = r.Get(4); n > 0 && r.Ok(); --n)
                        r.Get(6);

            players.push_back(p);
        }

        if (!r.Ok())
            players.clear();
        return players;
    }

    std::string RaceName(uint8 race)
    {
        ChrRacesEntry const* entry = sChrRacesStore.LookupEntry(race);
        return entry ? entry->name[0] : "?";
    }

    std::string ClassName(uint8 cls)
    {
        ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(cls);
        return entry ? entry->name[0] : "?";
    }

    std::string RollText(RolledPlayer const& p)
    {
        std::string text = RaceName(p.race) + " " + ClassName(p.cls);
        if (p.spec >= 0)
            text += Acore::StringFormat(", {}", SpecNames(p.cls)[p.spec]);
        std::string profs;
        for (int8 prof : p.profs)
            if (prof >= 0)
                profs += Acore::StringFormat("{}{}", profs.empty() ? "" : " + ", PROFESSION_NAMES[prof]);
        if (!profs.empty())
            text += ", " + profs;
        return text;
    }
}

class RollMgr
{
public:
    static RollMgr* instance()
    {
        static RollMgr mgr;
        return &mgr;
    }

    bool Enabled() const
    {
        return sWorldRandomizer->Config().moduleEnable && sConfigMgr->GetOption<bool>("Randomizer.Roll.Enable", true);
    }

    void Load(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::lock_guard lock(_lock);
        _rolls.erase(guid);
        if (QueryResult result = CharacterDatabase.Query("SELECT `name`, `race`, `class`, `spec`, `prof1`, `prof2` "
            "FROM `world_randomizer_roll` WHERE `guid` = {}", guid))
        {
            Field* fields = result->Fetch();
            RolledPlayer p;
            p.name = fields[0].Get<std::string>();
            p.race = fields[1].Get<uint8>();
            p.cls = fields[2].Get<uint8>();
            p.spec = fields[3].Get<int8>();
            p.profs = { fields[4].Get<int8>(), fields[5].Get<int8>() };
            _rolls[guid] = p;
        }
    }

    void Forget(Player* player)
    {
        std::lock_guard lock(_lock);
        _rolls.erase(player->GetGUID().GetCounter());
    }

    std::optional<RolledPlayer> Get(Player const* player)
    {
        std::lock_guard lock(_lock);
        auto itr = _rolls.find(player->GetGUID().GetCounter());
        if (itr == _rolls.end())
            return std::nullopt;
        return itr->second;
    }

    // Returns an error, or "" when the character is now tied to the roll
    std::string Bind(Player* player, std::string const& code, uint32 number, std::string& done)
    {
        if (!Enabled())
            return "Rolls are off on this server.";
        if (Get(player))
            return "This character already follows its roll (" + RollText(*Get(player)) + "). A GM can clear it.";

        std::vector<RolledPlayer> players = DecodeCode(code);
        if (players.empty())
            return "That is not a roller code (they start with WR5.). Copy it from the roller page.";
        if (number < 1 || number > players.size())
            return Acore::StringFormat("Pick a player from 1 to {}.", players.size());

        RolledPlayer const& p = players[number - 1];
        if (!p.race || !p.cls)
            return p.name + " has no roll in that code yet.";
        if (p.race != player->getRace() || p.cls != player->getClass())
            return Acore::StringFormat("{} rolled a {} {}; this character is a {} {}.", p.name, RaceName(p.race),
                ClassName(p.cls), RaceName(player->getRace()), ClassName(player->getClass()));

        // Primary professions it already has must be the rolled ones
        for (uint32 skill : PROFESSION_SKILLS)
            if (player->HasSkill(skill) && !Allows(p, skill))
                return Acore::StringFormat("Unlearn {} first: it isn't in the roll ({}).", SkillName(skill),
                    RollText(p));

        // Talents outside the rolled tree, per spec: the active spec's are given back for free; the other spec (dual
        // spec) would bring its own back on a switch, without the talent hook, so it has to be clean first
        bool offActive = false;
        bool offOther = false;
        if (p.spec >= 0)
            for (auto const& [spellId, talent] : player->GetTalentMap())
            {
                if (!talent || talent->State == PLAYERSPELL_REMOVED)
                    continue;
                TalentEntry const* entry = sTalentStore.LookupEntry(talent->talentID);
                TalentTabEntry const* tab = entry ? sTalentTabStore.LookupEntry(entry->TalentTab) : nullptr;
                if (!tab || int32(tab->tabpage) == p.spec)
                    continue;
                if (talent->IsInSpec(player->GetActiveSpec()))
                    offActive = true;
                if (talent->specMask & ~player->GetActiveSpecMask())
                    offOther = true;
            }
        if (offOther)
            return Acore::StringFormat("Your other spec has talents outside {}: switch to it, reset its talents at a "
                "trainer (or put them all in {}) and try again.", SpecNames(p.cls)[p.spec], SpecNames(p.cls)[p.spec]);
        if (offActive)
        {
            player->resetTalents(true);
            player->SendTalentsInfoData(false);
            done = " Your talents were reset for free, spend them in " + std::string(SpecNames(p.cls)[p.spec]) + ".";
        }

        std::string name = p.name;
        CharacterDatabase.EscapeString(name);
        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_roll` (`guid`, `name`, `race`, `class`, `spec`, "
            "`prof1`, `prof2`, `set_at`) VALUES ({}, '{}', {}, {}, {}, {}, {}, {})", player->GetGUID().GetCounter(),
            name, p.race, p.cls, p.spec, p.profs[0], p.profs[1], GameTime::GetGameTime().count());

        {
            std::lock_guard lock(_lock);
            _rolls[player->GetGUID().GetCounter()] = p;
        }

        // The rolled professions, learned right away, and the tools they need
        bool learn = sConfigMgr->GetOption<bool>("Randomizer.Roll.LearnProfessions", true);
        bool tools = sConfigMgr->GetOption<bool>("Randomizer.Roll.GiveTools", true);
        std::string given;
        for (int8 prof : p.profs)
        {
            if (prof < 0)
                continue;

            uint32 skill = PROFESSION_SKILLS[prof];
            // Within the server's primary profession limit (the trainer checks it; learnSpell doesn't)
            if (learn && !player->HasSkill(skill) && player->GetFreePrimaryProfessionPoints() > 0 &&
                sSpellMgr->GetSpellInfo(PROFESSION_APPRENTICE[prof]))
            {
                player->learnSpell(PROFESSION_APPRENTICE[prof]);
                if (player->HasSkill(skill))
                    given += Acore::StringFormat("{}{}", given.empty() ? "" : ", ", PROFESSION_NAMES[prof]);
            }

            uint32 tool = PROFESSION_TOOL[prof];
            if (tools && tool && player->HasSkill(skill) && !player->HasItemCount(tool, 1, true) &&
                sObjectMgr->GetItemTemplate(tool))
            {
                if (player->AddItem(tool, 1))
                    given += Acore::StringFormat("{}{}", given.empty() ? "" : ", ",
                        sObjectMgr->GetItemTemplate(tool)->Name1);
                else
                    done += Acore::StringFormat(" No room for the {}: buy it from a trade supplies vendor.",
                        sObjectMgr->GetItemTemplate(tool)->Name1);
            }
        }
        if (!given.empty())
            done = " You got: " + given + "." + done;

        return "";
    }

    static bool Allows(RolledPlayer const& p, uint32 skill)
    {
        for (int8 prof : p.profs)
            if (prof >= 0 && PROFESSION_SKILLS[prof] == skill)
                return true;
        // Nothing rolled for both slots: any profession
        return p.profs[0] < 0 && p.profs[1] < 0;
    }

    static std::string SkillName(uint32 skill)
    {
        SkillLineEntry const* entry = sSkillLineStore.LookupEntry(skill);
        return entry ? entry->name[0] : "?";
    }

    // The primary profession a spell (or a trainer entry wrapping one) would start, 0 if none
    static uint32 NewProfessionOf(Player const* player, uint32 spellId, uint8 depth = 0)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || depth > 2)
            return 0;

        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (effect.Effect == SPELL_EFFECT_SKILL_STEP || effect.Effect == SPELL_EFFECT_SKILL)
            {
                uint32 skill = uint32(effect.MiscValue);
                if (IsPrimaryProfessionSkill(skill) && !player->HasSkill(skill))
                    return skill;
            }
            else if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
                if (uint32 skill = NewProfessionOf(player, effect.TriggerSpell, depth + 1))
                    return skill;
        }
        return 0;
    }

private:
    std::mutex _lock;
    std::unordered_map<ObjectGuid::LowType, RolledPlayer> _rolls;
};

#define sRoll RollMgr::instance()

namespace
{
    void SendAddon(Player* player, std::string const& body)
    {
        std::string text = std::string(RANDOMIZER_ADDON_PREFIX) + "\t" + body;
        if (text.size() > 254)
            text.resize(254);

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, text);
        player->SendDirectMessage(&data);
    }

    // "R\t<on 0/1>\t<tied 0/1>\t<name>\t<race>\t<class>\t<spec>\t<prof1>\t<prof2>" (profession: roller index)
    void SendRoll(Player* player)
    {
        std::optional<RolledPlayer> roll = sRoll->Get(player);
        if (!roll)
        {
            SendAddon(player, Acore::StringFormat("R\t{}\t0", sRoll->Enabled() ? 1 : 0));
            return;
        }
        SendAddon(player, Acore::StringFormat("R\t1\t1\t{}\t{}\t{}\t{}\t{}\t{}", roll->name, roll->race, roll->cls,
            roll->spec, roll->profs[0], roll->profs[1]));
    }
}

// Addon requests (prefix RANDACH), answered whether achievements are on or not:
//   "ROLL"                  your roll                          -> R
//   "ROLLPEEK\t<code>"      the players in a code
//                           -> Q\t<number>\t<name>\t<race>\t<class>\t<text> per player, then E\tPEEK
//   "ROLLSET\t<n>\t<code>"  tie this character to player n     -> R, or X\t<error>
bool HandleRollAddonMessage(Player* player, std::string const& body)
{
    if (body == "ROLL")
    {
        SendRoll(player);
        return true;
    }

    if (body.compare(0, 9, "ROLLPEEK\t") == 0)
    {
        std::vector<RolledPlayer> players = DecodeCode(body.substr(9));
        for (std::size_t i = 0; i < players.size(); ++i)
            SendAddon(player, Acore::StringFormat("Q\t{}\t{}\t{}\t{}\t{}", i + 1, players[i].name, players[i].race,
                players[i].cls, players[i].race ? RollText(players[i]) : "no roll yet"));
        SendAddon(player, Acore::StringFormat("E\tPEEK\t{}", players.size()));
        return true;
    }

    if (body.compare(0, 8, "ROLLSET\t") == 0)
    {
        std::string rest = body.substr(8);
        std::size_t tab = rest.find('\t');
        uint32 number = tab == std::string::npos ? 0 : uint32(std::atoi(rest.substr(0, tab).c_str()));
        std::string done;
        std::string error = tab == std::string::npos ? "Bad request." :
            sRoll->Bind(player, rest.substr(tab + 1), number, done);
        if (!error.empty())
            SendAddon(player, "X\t" + error);
        else
            ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[Roll]|r This character now follows its "
                "roll: {}.{}", RollText(*sRoll->Get(player)), done);
        SendRoll(player);
        return true;
    }

    return false;
}

class RollPlayerScript : public PlayerScript
{
public:
    RollPlayerScript() : PlayerScript("RollPlayerScript", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_CAN_LEARN_TALENT,
        PLAYERHOOK_ON_GET_TRAINER_SPELL_STATE,
        PLAYERHOOK_CAN_LEARN_SPELL
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (sRoll->Enabled())
            sRoll->Load(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        sRoll->Forget(player);
    }

    // Talents only in the rolled tree
    bool OnPlayerCanLearnTalent(Player* player, TalentEntry const* talent, uint32 /*rank*/) override
    {
        if (!sRoll->Enabled())
            return true;

        std::optional<RolledPlayer> roll = sRoll->Get(player);
        if (!roll || roll->spec < 0)
            return true;

        TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
        if (!tab || (tab->ClassMask & player->getClassMask()) == 0 || int32(tab->tabpage) == roll->spec)
            return true;

        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[Roll]|r Your roll is {}: talents only go into "
            "that tree.", SpecNames(roll->cls)[roll->spec]);
        return false;
    }

    // Trainers show other primary professions as unavailable
    void OnPlayerGetTrainerSpellState(Player const* player, uint32 /*trainerId*/, uint32 spellId,
        Trainer::SpellState& state) override
    {
        if (state != Trainer::SpellState::Available || !sRoll->Enabled())
            return;

        std::optional<RolledPlayer> roll = sRoll->Get(player);
        if (!roll)
            return;

        if (uint32 skill = RollMgr::NewProfessionOf(player, spellId))
            if (!RollMgr::Allows(*roll, skill))
                state = Trainer::SpellState::Unavailable;
    }

    // And won't teach them, whatever path (trainer, book, quest) tries
    bool OnPlayerCanLearnSpell(Player* player, uint32 spellId) override
    {
        if (!sRoll->Enabled())
            return true;

        std::optional<RolledPlayer> roll = sRoll->Get(player);
        if (!roll)
            return true;

        uint32 skill = RollMgr::NewProfessionOf(player, spellId);
        if (!skill || RollMgr::Allows(*roll, skill))
            return true;

        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[Roll]|r {} isn't in your roll ({}).",
            RollMgr::SkillName(skill), RollText(*roll));
        return false;
    }
};

    // .randomizer roll                          your roll
    // .randomizer roll <code>                   the players in a code
    // .randomizer roll <code> <player number>   tie this character to that player's roll (for good)
bool HandleRandomizerRollCommand(ChatHandler* handler, Optional<std::string> code, Optional<uint32> number)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!code)
        {
            std::optional<RolledPlayer> roll = sRoll->Get(player);
            if (roll)
                handler->PSendSysMessage("This character follows {}'s roll: {}.", roll->name, RollText(*roll));
            else
                handler->SendSysMessage("This character isn't tied to a roll. .randomizer roll <code> <player number> "
                    "ties it (talents only in the rolled tree, only the rolled professions).");
            return true;
        }

        if (!number)
        {
            std::vector<RolledPlayer> players = DecodeCode(*code);
            if (players.empty())
            {
                handler->SendErrorMessage("That is not a roller code (they start with WR5.).");
                return false;
            }
            for (std::size_t i = 0; i < players.size(); ++i)
                handler->PSendSysMessage("  {}: {} - {}", i + 1, players[i].name,
                    players[i].race ? RollText(players[i]) : "no roll yet");
            handler->SendSysMessage("Add the player number to tie this character to that roll.");
            return true;
        }

        std::string done;
        std::string error = sRoll->Bind(player, *code, *number, done);
        if (!error.empty())
        {
            handler->SendErrorMessage(error, false);
            return false;
        }

        handler->PSendSysMessage("|cffFF8000[Roll]|r This character now follows its roll: {}.{}",
            RollText(*sRoll->Get(player)), done);
        SendRoll(player);
        return true;
    }

bool HandleRandomizerRollClearCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        CharacterDatabase.Execute("DELETE FROM `world_randomizer_roll` WHERE `guid` = {}",
            target.GetGUID().GetCounter());
        if (Player* player = target.GetConnectedPlayer())
        {
            sRoll->Forget(player);
            SendRoll(player);
        }
        handler->PSendSysMessage("{} no longer follows a roll.", target.GetName());
        return true;
}

void AddWorldRandomizerRollScripts()
{
    new RollPlayerScript();
}
