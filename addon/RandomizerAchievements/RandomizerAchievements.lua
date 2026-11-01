-- Randomizer Achievements (WoW 3.3.5)
-- Talks to mod-world-randomizer with addon whispers to yourself (prefix RANDACH):
--   asks for the achievement list once, then for the progress of you or a party / raid member.
-- If the server doesn't answer soon after logging in, the addon asks again a few times; after that it hides until
-- an answer comes (or /randach asks once more). A server without the module just never answers.
-- /randach            show or hide the window
-- /randach <name>     show a group member's achievements
-- /randach group      the Group tab: start alone or with a co-op party, and your party's info
-- /randach roll       tie this character to its roll from the roller page
-- /randach world      the World tab: every zone and the level bracket the shuffle gave it

local PREFIX = "RANDACH"
local ADDON_VERSION = 6   -- must match RANDOMIZER_ADDON_VERSION in the module (src/WorldRandomizerAchievements.h)
local serverVersion      -- what the module expects, from its "V" reply
local ROW_HEIGHT = 50
local ROWS = 8

RandomizerAchievementsDB = RandomizerAchievementsDB or {}

local defs = {}          -- [id] = { cat, icon, name, reward, desc, exp (mask), parent }
local order = {}         -- ids in server order
local categories = {}    -- { { id, name } }
local progress = {}      -- [name] = { stats = {...}, ach = { [id] = { done, prog, req } } }
local incoming = {}      -- progress being received, per name
local selectedCategory = 0
local viewing = UnitName("player")
local defsLoaded = false
local expansion = 3      -- the server's content: 1 Vanilla, 2 TBC, 3 Wrath
local available = nil    -- nil: waiting for the server, true: it answered, false: no answer yet (or module off)
local replyTimeout
local askTries = 0       -- how many times the server was asked without an answer
local ASK_TRIES = 4      -- asks (10 seconds apart) before hiding
local moduleOff = false  -- the server said the World Randomizer is off
local minimapButton, optionsPanel
local autoRefresh = 0    -- seconds until the open window asks the server again
local achievementsOff = false -- the server said achievements are off ("My roll" still works)
local EXPANSION_NAMES = { "Vanilla", "TBC", "Wrath" }
local currentTab = 1     -- 1 achievements, 2 group, 3 world (zones and their brackets)
local SelectTab, ShowPopupNow
local function UI_DB_POPUP()
    RandomizerAchievementsDB = RandomizerAchievementsDB or {}
    return RandomizerAchievementsDB
end
local achWidgets = {}    -- what the achievements tab shows (hidden on the group tab)
-- This character, from the server's "H": hardcore (0 no, 1 alive, 2 dead), whether its play counts for the
-- achievements, the server's hardcore mode, and whether it may try "Go hardcore"
local challenge = { hardcore = 0, counts = nil, mode = 0, canTry = false }

-- Made for the server's current content?
local function IsActive(def)
    if not def.exp or def.exp == 0 then return true end
    local bit = 2 ^ (expansion - 1)
    return def.exp % (bit * 2) >= bit
end

local function ExpansionText(mask)
    if not mask or mask == 0 then return "every expansion" end
    local names = {}
    for i = 1, 3 do
        local bit = 2 ^ (i - 1)
        if mask % (bit * 2) >= bit then table.insert(names, EXPANSION_NAMES[i]) end
    end
    return table.concat(names, ", ")
end

-- The other versions of an achievement (same parent), for the tooltip
local function Versions(id)
    local def = defs[id]
    local base = (def.parent and def.parent > 0) and def.parent or id
    local list = {}
    for _, other in ipairs(order) do
        local o = defs[other]
        local oBase = (o.parent and o.parent > 0) and o.parent or other
        if other ~= id and oBase == base then table.insert(list, other) end
    end
    return list
end

local function Send(body)
    SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player"))
end

local function Split(text, sep)
    local out = {}
    for piece in string.gmatch(text .. sep, "(.-)" .. sep) do
        table.insert(out, piece)
    end
    return out
end

-- ---------------------------------------------------------------- window

local frame = CreateFrame("Frame", "RandomizerAchievementsFrame", UIParent)
frame:SetWidth(720); frame:SetHeight(560)
frame:SetPoint("CENTER")
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
frame:Hide()
table.insert(UISpecialFrames, "RandomizerAchievementsFrame") -- Escape closes it

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -18)
title:SetText("Randomizer Achievements")

local summary = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
summary:SetPoint("TOP", title, "BOTTOM", 0, -6)
summary:SetText("")

-- Category list
local catButtons = {}
local function CategoryButton(index)
    local button = catButtons[index]
    if button then return button end

    button = CreateFrame("Button", nil, frame)
    button:SetWidth(170); button:SetHeight(18)
    button:SetPoint("TOPLEFT", 20, -58 - (index - 1) * 19)
    button:SetNormalFontObject("GameFontNormalSmall")
    button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    local text = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    text:SetPoint("LEFT", 4, 0)
    text:SetJustifyH("LEFT")
    text:SetWidth(166)
    button.text = text
    catButtons[index] = button
    table.insert(achWidgets, button)
    return button
end

-- Achievement rows
local scroll = CreateFrame("ScrollFrame", "RandomizerAchievementsScroll", frame, "FauxScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 200, -58)
scroll:SetWidth(470); scroll:SetHeight(ROWS * ROW_HEIGHT)
table.insert(achWidgets, scroll)

local rows = {}
for i = 1, ROWS do
    local row = CreateFrame("Frame", nil, frame)
    row:SetWidth(470); row:SetHeight(ROW_HEIGHT - 2)
    row:SetPoint("TOPLEFT", scroll, "TOPLEFT", 0, -(i - 1) * ROW_HEIGHT)

    row.bg = row:CreateTexture(nil, "BACKGROUND")
    row.bg:SetAllPoints()
    row.bg:SetTexture(0, 0, 0, 0.35)

    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetWidth(36); row.icon:SetHeight(36)
    row.icon:SetPoint("LEFT", 4, 0)

    row.check = row:CreateTexture(nil, "OVERLAY")
    row.check:SetWidth(18); row.check:SetHeight(18)
    row.check:SetPoint("BOTTOMRIGHT", row.icon, "BOTTOMRIGHT", 4, -4)
    row.check:SetTexture("Interface\\RaidFrame\\ReadyCheck-Ready")

    -- One line each (cut with "..."); the tooltip has everything
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, 2)
    row.name:SetWidth(290); row.name:SetHeight(16)
    row.name:SetJustifyH("LEFT")

    row.reward = row:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
    row.reward:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, 0)
    row.reward:SetWidth(290); row.reward:SetHeight(14)
    row.reward:SetJustifyH("LEFT")

    row.desc = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    row.desc:SetPoint("TOPLEFT", row.reward, "BOTTOMLEFT", 0, 0)
    row.desc:SetWidth(290); row.desc:SetHeight(14)
    row.desc:SetJustifyH("LEFT")

    row.bar = CreateFrame("StatusBar", nil, row)
    row.bar:SetWidth(120); row.bar:SetHeight(14)
    row.bar:SetPoint("RIGHT", -8, 0)
    row.bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    row.bar:SetStatusBarColor(0.1, 0.6, 0.1)
    row.bar:SetMinMaxValues(0, 1)
    row.bar.bg = row.bar:CreateTexture(nil, "BACKGROUND")
    row.bar.bg:SetAllPoints()
    row.bar.bg:SetTexture(0, 0, 0, 0.6)
    row.bar.text = row.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.bar.text:SetPoint("CENTER")

    -- Hover: which content this version is for, and the other versions
    row:EnableMouse(true)
    row:SetScript("OnEnter", function(self)
        local def = self.id and defs[self.id]
        if not def then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(def.name, 1, 0.82, 0)
        if def.desc ~= "" then GameTooltip:AddLine(def.desc, 1, 1, 1, true) end
        if def.reward ~= "" then GameTooltip:AddLine(def.reward, 0.1, 1, 0.1, true) end
        GameTooltip:AddLine("This version: " .. ExpansionText(def.exp), 0.53, 0.53, 1)
        local versions = Versions(self.id)
        if #versions > 0 then
            GameTooltip:AddLine(" ")
            GameTooltip:AddLine("Other versions:", 1, 0.82, 0)
            for _, other in ipairs(versions) do
                local o = defs[other]
                GameTooltip:AddLine(ExpansionText(o.exp) .. ": " .. (o.reward ~= "" and o.reward or o.name),
                    0.7, 0.7, 0.7, true)
            end
        end
        GameTooltip:Show()
    end)
    row:SetScript("OnLeave", function() GameTooltip:Hide() end)

    rows[i] = row
    table.insert(achWidgets, row)
end

-- Whose achievements: you and your group
local memberButtons = {}
local refresh = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
refresh:SetWidth(80); refresh:SetHeight(22)
refresh:SetPoint("BOTTOMRIGHT", -20, 18)
refresh:SetText("Refresh")

local myRoll = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
myRoll:SetWidth(80); myRoll:SetHeight(22)
myRoll:SetPoint("RIGHT", refresh, "LEFT", -6, 0)
myRoll:SetText("My roll")
myRoll:SetScript("OnClick", function() SlashCmdList["RANDACH"]("roll") end)

local whoLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
whoLabel:SetPoint("BOTTOMLEFT", 22, 23)
whoLabel:SetText("Show:")
table.insert(achWidgets, whoLabel)
table.insert(achWidgets, summary)

local function GroupMembers()
    local names = { UnitName("player") }
    local raid = GetNumRaidMembers()
    if raid > 0 then
        for i = 1, raid do
            local name = UnitName("raid" .. i)
            if name and name ~= names[1] then table.insert(names, name) end
        end
    else
        for i = 1, GetNumPartyMembers() do
            local name = UnitName("party" .. i)
            if name then table.insert(names, name) end
        end
    end
    return names
end

-- ---------------------------------------------------------------- drawing

local Request

local function VisibleIds()
    local list = {}
    for _, id in ipairs(order) do
        if IsActive(defs[id]) and (selectedCategory == 0 or defs[id].cat == selectedCategory) then
            table.insert(list, id)
        end
    end
    return list
end

local function UpdateRows()
    local list = VisibleIds()
    local data = progress[viewing]
    FauxScrollFrame_Update(scroll, #list, ROWS, ROW_HEIGHT)
    local offset = FauxScrollFrame_GetOffset(scroll)

    for i = 1, ROWS do
        local row = rows[i]
        local id = list[i + offset]
        if id then
            local def = defs[id]
            local state = data and data.ach[id]
            local done = state and state.done
            row.icon:SetTexture("Interface\\Icons\\" .. (def.icon ~= "" and def.icon or "INV_Misc_QuestionMark"))
            row.icon:SetDesaturated(not done)
            if done then row.check:Show() else row.check:Hide() end
            local tag = ""
            if def.exp and def.exp ~= 0 then tag = " |cff8888ff[" .. ExpansionText(def.exp) .. "]|r" end
            row.name:SetText(def.name .. tag)
            row.id = id
            if done then row.name:SetTextColor(1, 0.82, 0) else row.name:SetTextColor(0.9, 0.9, 0.9) end
            row.reward:SetText(def.reward ~= "" and def.reward or "")
            row.desc:SetText(def.desc or "")
            if state then
                row.bar:SetMinMaxValues(0, math.max(1, state.req))
                row.bar:SetValue(state.prog)
                row.bar.text:SetText(state.prog .. " / " .. state.req)
                row.bar:Show()
            else
                row.bar:Hide()
            end
            row:Show()
        else
            row:Hide()
        end
    end
end

scroll:SetScript("OnVerticalScroll", function(self, offset)
    FauxScrollFrame_OnVerticalScroll(self, offset, ROW_HEIGHT, UpdateRows)
end)

local function UpdateCategories()
    local list = { { 0, "All" } }
    for _, cat in ipairs(categories) do table.insert(list, cat) end

    local data = progress[viewing]
    for index, cat in ipairs(list) do
        local button = CategoryButton(index)
        local done, total = 0, 0
        for _, id in ipairs(order) do
            if IsActive(defs[id]) and (cat[1] == 0 or defs[id].cat == cat[1]) then
                total = total + 1
                if data and data.ach[id] and data.ach[id].done then done = done + 1 end
            end
        end
        button.text:SetText(string.format("%s |cff999999(%d/%d)|r", cat[2], done, total))
        if cat[1] == selectedCategory then
            button.text:SetTextColor(1, 1, 1)
        else
            button.text:SetTextColor(1, 0.82, 0)
        end
        button:SetScript("OnClick", function()
            selectedCategory = cat[1]
            FauxScrollFrame_SetOffset(scroll, 0)
            scroll:SetVerticalScroll(0)
            UpdateCategories()
            UpdateRows()
        end)
        button:Show()
    end
    for index = #list + 1, #catButtons do catButtons[index]:Hide() end
end

local function UpdateMembers()
    local names = GroupMembers()
    for index, name in ipairs(names) do
        local button = memberButtons[index]
        if not button then
            button = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
            button:SetWidth(90); button:SetHeight(22)
            button:SetPoint("BOTTOMLEFT", 62 + (index - 1) * 92, 18)
            memberButtons[index] = button
            table.insert(achWidgets, button)
        end
        button:SetText(index == 1 and "Me" or name)
        button:SetScript("OnClick", function()
            viewing = name
            Request(name)
        end)
        if name == viewing then button:LockHighlight() else button:UnlockHighlight() end
        button:Show()
    end
    for index = #names + 1, #memberButtons do memberButtons[index]:Hide() end
end

local function UpdateSummary()
    local data = progress[viewing]
    title:SetText("Randomizer Achievements - " .. viewing .. "  |cff8888ff(" .. EXPANSION_NAMES[expansion] .. ")|r")
    if not data then
        summary:SetText(defsLoaded and "Asking the server..." or "Loading the achievement list...")
        return
    end
    local s = data.stats
    local cap = (s.cap and s.cap > 0) and ("   Level cap " .. s.cap) or ""
    local counts = ""
    if viewing == UnitName("player") and challenge.counts ~= nil then
        counts = challenge.counts and "   |cff4CFF00This character counts|r" or
            "   |cffFF6060This character doesn't count yet|r"
    end
    summary:SetText(string.format("%d / %d done   Deaths %d   Zones %d   This run: %d quests, %d rares, %dg%s%s",
        s.done, s.total, s.deaths, s.zones, s.quests, s.rares, s.gold, cap, counts))
end

local function Redraw()
    if not frame:IsShown() or currentTab ~= 1 then return end
    UpdateSummary()
    UpdateCategories()
    UpdateMembers()
    UpdateRows()
end

-- ---------------------------------------------------------------- messages

Request = function(name)
    if name == UnitName("player") then Send("GET") else Send("GET\t" .. name) end
    Redraw()
end

local RequestGroup
refresh:SetScript("OnClick", function()
    if currentTab == 2 then RequestGroup() elseif currentTab == 3 then Send("ZONES") else Request(viewing) end
end)

local Disable, Revive

local function Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffFFD100[Randomizer Achievements]|r " .. text)
end


-- ---------------------------------------------------------------- my roll (the web roller's code)

local RACE_NAMES = { [1] = "Human", [2] = "Orc", [3] = "Dwarf", [4] = "Night Elf", [5] = "Undead", [6] = "Tauren",
    [7] = "Gnome", [8] = "Troll", [10] = "Blood Elf", [11] = "Draenei" }
local CLASS_NAMES = { [1] = "Warrior", [2] = "Paladin", [3] = "Hunter", [4] = "Rogue", [5] = "Priest",
    [6] = "Death Knight", [7] = "Shaman", [8] = "Mage", [9] = "Warlock", [11] = "Druid" }
local CLASS_FILES = { [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
    [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID" }
local RACE_FILES = { [1] = "Human", [2] = "Orc", [3] = "Dwarf", [4] = "NightElf", [5] = "Scourge", [6] = "Tauren",
    [7] = "Gnome", [8] = "Troll", [10] = "BloodElf", [11] = "Draenei" }
local SPEC_NAMES = {
    [1] = { "Arms", "Fury", "Protection" }, [2] = { "Holy", "Protection", "Retribution" },
    [3] = { "Beast Mastery", "Marksmanship", "Survival" }, [4] = { "Assassination", "Combat", "Subtlety" },
    [5] = { "Discipline", "Holy", "Shadow" }, [6] = { "Blood", "Frost", "Unholy" },
    [7] = { "Elemental", "Enhancement", "Restoration" }, [8] = { "Arcane", "Fire", "Frost" },
    [9] = { "Affliction", "Demonology", "Destruction" }, [11] = { "Balance", "Feral", "Restoration" } }
local SPEC_ICONS = {
    [1] = { "ability_warrior_savageblow", "ability_warrior_innerrage", "ability_warrior_defensivestance" },
    [2] = { "spell_holy_holybolt", "spell_holy_devotionaura", "spell_holy_auraoflight" },
    [3] = { "ability_hunter_beasttaming", "ability_marksmanship", "ability_hunter_swiftstrike" },
    [4] = { "ability_rogue_eviscerate", "ability_backstab", "ability_stealth" },
    [5] = { "spell_holy_powerwordshield", "spell_holy_guardianspirit", "spell_shadow_shadowwordpain" },
    [6] = { "spell_deathknight_bloodpresence", "spell_deathknight_frostpresence", "spell_deathknight_unholypresence" },
    [7] = { "spell_nature_lightning", "spell_nature_lightningshield", "spell_nature_magicimmunity" },
    [8] = { "spell_holy_magicalsentry", "spell_fire_firebolt02", "spell_frost_frostbolt02" },
    [9] = { "spell_shadow_deathcoil", "spell_shadow_metamorphosis", "spell_shadow_rainoffire" },
    [11] = { "spell_nature_starfall", "ability_racial_bearform", "spell_nature_healingtouch" } }
local PROF_NAMES = { [0] = "Alchemy", "Blacksmithing", "Enchanting", "Engineering", "Herbalism", "Inscription",
    "Jewelcrafting", "Leatherworking", "Mining", "Skinning", "Tailoring" }
local PROF_ICONS = { [0] = "trade_alchemy", "trade_blacksmithing", "trade_engraving", "trade_engineering",
    "trade_herbalism", "inv_inscription_tradeskill01", "inv_misc_gem_01", "trade_leatherworking", "trade_mining",
    "inv_misc_pelt_wolf_01", "trade_tailoring" }

local function Shown(widget, on)
    if on then widget:Show() else widget:Hide() end
end

local rollFrame = CreateFrame("Frame", "RandomizerRollFrame", UIParent)
rollFrame:SetWidth(460); rollFrame:SetHeight(390)
rollFrame:SetPoint("CENTER", 0, 40)
rollFrame:SetMovable(true)
rollFrame:EnableMouse(true)
rollFrame:RegisterForDrag("LeftButton")
rollFrame:SetScript("OnDragStart", rollFrame.StartMoving)
rollFrame:SetScript("OnDragStop", rollFrame.StopMovingOrSizing)
rollFrame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
rollFrame:SetFrameStrata("DIALOG")
rollFrame:Hide()
table.insert(UISpecialFrames, "RandomizerRollFrame")

local rollClose = CreateFrame("Button", nil, rollFrame, "UIPanelCloseButton")
rollClose:SetPoint("TOPRIGHT", -6, -6)

local rollTitle = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
rollTitle:SetPoint("TOP", 0, -18)
rollTitle:SetText("My roll")

local rollStatus = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
rollStatus:SetPoint("TOPLEFT", 24, -48)
rollStatus:SetWidth(410)
rollStatus:SetJustifyH("LEFT")

-- The tied roll: spec and profession icons with their names on hover
local rollIcons = {}
for i = 1, 3 do
    local icon = CreateFrame("Frame", nil, rollFrame)
    icon:SetWidth(36); icon:SetHeight(36)
    icon:SetPoint("TOPLEFT", 24 + (i - 1) * 44, -96)
    icon.tex = icon:CreateTexture(nil, "ARTWORK")
    icon.tex:SetAllPoints()
    icon:EnableMouse(true)
    icon:SetScript("OnEnter", function(self)
        if not self.tip then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine(self.tip)
        GameTooltip:Show()
    end)
    icon:SetScript("OnLeave", function() GameTooltip:Hide() end)
    icon:Hide()
    rollIcons[i] = icon
end

local codeLabel = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
codeLabel:SetPoint("TOPLEFT", 24, -142)
codeLabel:SetText("Party or player code from the roller page (WR5....):")

local codeBox = CreateFrame("EditBox", "RandomizerRollCode", rollFrame, "InputBoxTemplate")
codeBox:SetWidth(320); codeBox:SetHeight(20)
codeBox:SetPoint("TOPLEFT", 30, -158)
codeBox:SetAutoFocus(false)
codeBox:SetMaxLetters(230)

local readButton = CreateFrame("Button", nil, rollFrame, "UIPanelButtonTemplate")
readButton:SetWidth(80); readButton:SetHeight(22)
readButton:SetPoint("LEFT", codeBox, "RIGHT", 8, 0)
readButton:SetText("Read")

local pickLabel = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
pickLabel:SetPoint("TOPLEFT", 24, -190)
pickLabel:SetText("Who are you? (only players of your race and class can be picked)")

local peek = {}
local pickButtons = {}
local function PickButton(index)
    local button = pickButtons[index]
    if button then return button end
    button = CreateFrame("Button", nil, rollFrame, "UIPanelButtonTemplate")
    button:SetWidth(410); button:SetHeight(22)
    button:SetPoint("TOPLEFT", 24, -206 - (index - 1) * 24)
    pickButtons[index] = button
    return button
end

local rollState = { on = false, tied = false }

local function UpdateRoll()
    for i = 1, 3 do rollIcons[i]:Hide() end
    if rollState.tied then
        local r = rollState
        local text = "|cffFFD100" .. r.name .. "'s roll:|r " .. (RACE_NAMES[r.race] or "?") .. " " ..
            (CLASS_NAMES[r.cls] or "?")
        local slot = 1
        if r.spec >= 0 and SPEC_NAMES[r.cls] then
            text = text .. ", " .. SPEC_NAMES[r.cls][r.spec + 1]
            rollIcons[slot].tex:SetTexture("Interface\\Icons\\" .. SPEC_ICONS[r.cls][r.spec + 1])
            rollIcons[slot].tip = SPEC_NAMES[r.cls][r.spec + 1] .. " (talents only go into this tree)"
            rollIcons[slot]:Show()
            slot = slot + 1
        end
        for _, prof in ipairs({ r.prof1, r.prof2 }) do
            if prof >= 0 and PROF_NAMES[prof] then
                text = text .. ", " .. PROF_NAMES[prof]
                rollIcons[slot].tex:SetTexture("Interface\\Icons\\" .. PROF_ICONS[prof])
                rollIcons[slot].tip = PROF_NAMES[prof]
                rollIcons[slot]:Show()
                slot = slot + 1
            end
        end
        rollStatus:SetText(text .. "\n|cff999999This character follows it for good: talents only in that tree, " ..
            "only these primary professions.|r")
    elseif rollState.on then
        rollStatus:SetText("This character isn't tied to a roll. Paste the code from the roller page, press Read, " ..
            "then pick who you are. |cffFF6060It can't be undone|r (a GM can).")
    else
        rollStatus:SetText("Rolls are off on this server.")
    end

    local canPick = rollState.on and not rollState.tied
    Shown(codeBox, canPick)
    Shown(readButton, canPick)
    Shown(codeLabel, canPick)
    Shown(pickLabel, canPick and #peek > 0)
    local _, myClass = UnitClass("player")
    local _, myRace = UnitRace("player")
    for index, entry in ipairs(peek) do
        local button = PickButton(index)
        button:SetText(entry.number .. ": " .. entry.name .. " - " .. entry.text)
        local fits = CLASS_FILES[entry.cls] == myClass and RACE_FILES[entry.race] == myRace
        if fits then button:Enable() else button:Disable() end
        button:SetScript("OnClick", function()
            StaticPopupDialogs["RANDOMIZER_ROLL_CONFIRM"] = {
                text = "Tie this character to " .. entry.name .. "'s roll (" .. entry.text .. ")?\n\nTalents will " ..
                    "only go into that tree and trainers will only teach those professions. This can't be undone.",
                button1 = YES, button2 = NO, timeout = 0, whileDead = true, hideOnEscape = true,
                OnAccept = function() Send("ROLLSET\t" .. entry.number .. "\t" .. rollState.code) end,
            }
            StaticPopup_Show("RANDOMIZER_ROLL_CONFIRM")
        end)
        Shown(button, canPick)
    end
    for index = #peek + 1, #pickButtons do pickButtons[index]:Hide() end
end

readButton:SetScript("OnClick", function()
    local code = codeBox:GetText():gsub("%s", "")
    if code == "" then return end
    rollState.code = code
    peek = {}
    Send("ROLLPEEK\t" .. code)
end)
codeBox:SetScript("OnEnterPressed", function() readButton:Click() end)
codeBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)

rollFrame:SetScript("OnShow", function()
    Send("ROLL")
    UpdateRoll()
end)

-- R / Q / E PEEK from the server; true when it was one of them
local function OnRollMessage(parts)
    local kind = parts[1]
    if kind == "R" then
        rollState.on = parts[2] == "1"
        rollState.tied = parts[3] == "1"
        if rollState.tied then
            rollState.name = parts[4] or "?"
            rollState.race = tonumber(parts[5]) or 0
            rollState.cls = tonumber(parts[6]) or 0
            rollState.spec = tonumber(parts[7]) or -1
            rollState.prof1 = tonumber(parts[8]) or -1
            rollState.prof2 = tonumber(parts[9]) or -1
            peek = {}
        end
        if rollFrame:IsShown() then UpdateRoll() end
        return true
    elseif kind == "Q" then
        table.insert(peek, { number = tonumber(parts[2]), name = parts[3] or "?", race = tonumber(parts[4]) or 0,
            cls = tonumber(parts[5]) or 0, text = parts[6] or "" })
        return true
    elseif kind == "E" and parts[2] == "PEEK" then
        if #peek == 0 then Print("That is not a roller code (they start with WR5.).") end
        if rollFrame:IsShown() then UpdateRoll() end
        return true
    end
    return false
end

-- ---------------------------------------------------------------- group tab (co-op start and party info)

local start = { state = nil, code = "", members = 0, maxSize = 5, maxLevel = 1, canChoose = false, why = "" }
local startZones, startZonesIncoming = {}, {}
local party, partyIncoming = {}, {}
local groupError       -- the server's answer to the last choice, when it was refused
local waitingChoice    -- a choice was sent, its answer not back yet
local startPrompted    -- the chooser opened by itself once this session
local chosenZone = 0   -- 0: a random start zone
local PARTY_ROWS = 10
local ROW_TAGS = { { 1, "|cffFFD100Leader|r" }, { 8, "|cff88CCFFYou|r" } }

local groupPanel = CreateFrame("Frame", nil, frame)
groupPanel:SetPoint("TOPLEFT", 22, -46)
groupPanel:SetPoint("BOTTOMRIGHT", -22, 46)
groupPanel:Hide()

local groupStatus = groupPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
groupStatus:SetPoint("TOPLEFT", 0, -4)
groupStatus:SetWidth(670)
groupStatus:SetJustifyH("LEFT")

local groupErrorText = groupPanel:CreateFontString(nil, "OVERLAY", "GameFontRedSmall")
groupErrorText:SetPoint("TOPLEFT", groupStatus, "BOTTOMLEFT", 0, -4)
groupErrorText:SetWidth(670)
groupErrorText:SetJustifyH("LEFT")

-- This character: hardcore or not, and whether it counts for the achievements
local statusLine = groupPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
statusLine:SetPoint("BOTTOMLEFT", 0, 4)
statusLine:SetWidth(520)
statusLine:SetJustifyH("LEFT")

local hardcoreButton = CreateFrame("Button", nil, groupPanel, "UIPanelButtonTemplate")
hardcoreButton:SetWidth(140); hardcoreButton:SetHeight(22)
hardcoreButton:SetPoint("BOTTOMRIGHT", 0, 0)
hardcoreButton:SetText("Go hardcore")
hardcoreButton:Hide()

-- The chooser: alone, or a party (make one, or join one with a code)
local chooser = CreateFrame("Frame", nil, groupPanel)
chooser:SetPoint("TOPLEFT", 0, -46)
chooser:SetWidth(670); chooser:SetHeight(184)

local function BigButton(parent, text, width)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetWidth(width or 180); button:SetHeight(26)
    button:SetText(text)
    return button
end

local zoneLabel = chooser:CreateFontString(nil, "OVERLAY", "GameFontNormal")
zoneLabel:SetPoint("TOPLEFT", 0, -8)
zoneLabel:SetText("Start zone:")

local zoneMenu = CreateFrame("Frame", "RandomizerStartZoneMenu", chooser, "UIDropDownMenuTemplate")
zoneMenu:SetPoint("LEFT", zoneLabel, "RIGHT", -8, -2)
UIDropDownMenu_SetWidth(zoneMenu, 260)

local zoneHint = chooser:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
zoneHint:SetPoint("LEFT", zoneMenu, "RIGHT", -6, 2)
zoneHint:SetText("levels after the shuffle (and before); joining goes where the party starts")

local aloneButton = BigButton(chooser, "Start alone")
aloneButton:SetPoint("TOPLEFT", 0, -34)
local partyButton = BigButton(chooser, "Start with a party")
partyButton:SetPoint("LEFT", aloneButton, "RIGHT", 12, 0)

local chooserHelp = chooser:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
chooserHelp:SetPoint("TOPLEFT", aloneButton, "BOTTOMLEFT", 0, -6)
chooserHelp:SetWidth(670)
chooserHelp:SetJustifyH("LEFT")
chooserHelp:SetText("Alone: you play on your own; this character can never make or join a co-op party. " ..
    "With a party: everyone starts in the same place, with their hearthstone there, in one group. " ..
    "Same as .coop create and .coop join in chat.")

local partyBox = CreateFrame("Frame", nil, chooser)
partyBox:SetPoint("TOPLEFT", chooserHelp, "BOTTOMLEFT", 0, -10)
partyBox:SetWidth(670); partyBox:SetHeight(80)
partyBox:Hide()

local makeLabel = partyBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
makeLabel:SetPoint("TOPLEFT", 0, 0)
makeLabel:SetText("Make a party (in the start zone above)")

local function ZoneName(id, plain)
    if id == 0 then return "Don't pick one" end
    for _, zone in ipairs(startZones) do
        if zone.id == id then
            if plain or not zone.bracket then return zone.name end
            return zone.name .. " (" .. zone.bracket .. (zone.was ~= "" and (", was " .. zone.was) or "") .. ")"
        end
    end
    return "Zone " .. id
end

UIDropDownMenu_Initialize(zoneMenu, function()
    local list = { 0 }
    for _, zone in ipairs(startZones) do table.insert(list, zone.id) end
    for _, id in ipairs(list) do
        local info = UIDropDownMenu_CreateInfo()
        info.text = ZoneName(id)
        info.checked = id == chosenZone
        info.func = function()
            chosenZone = id
            UIDropDownMenu_SetText(zoneMenu, ZoneName(id))
        end
        UIDropDownMenu_AddButton(info)
    end
end)

local createButton = BigButton(partyBox, "Create", 100)
createButton:SetPoint("TOPLEFT", makeLabel, "BOTTOMLEFT", 0, -6)

local joinLabel = partyBox:CreateFontString(nil, "OVERLAY", "GameFontNormal")
joinLabel:SetPoint("TOPLEFT", 380, 0)
joinLabel:SetText("Join a party (the 6 letter code)")

local joinBox = CreateFrame("EditBox", "RandomizerJoinCode", partyBox, "InputBoxTemplate")
joinBox:SetWidth(120); joinBox:SetHeight(20)
joinBox:SetPoint("TOPLEFT", joinLabel, "BOTTOMLEFT", 6, -8)
joinBox:SetAutoFocus(false)
joinBox:SetMaxLetters(8)

local joinButton = BigButton(partyBox, "Join", 100)
joinButton:SetPoint("LEFT", joinBox, "RIGHT", 8, 0)

-- Every choice is for good, so each one asks first
local function Confirm(what, request, after)
    StaticPopupDialogs["RANDOMIZER_START_CONFIRM"] = {
        text = what .. "\n\nAre you sure you want to pick this option? You cannot redo this option.",
        button1 = YES, button2 = NO, timeout = 0, whileDead = true, hideOnEscape = true,
        OnAccept = function()
            groupError = nil
            waitingChoice = true
            Send(request)
            if after then Send(after) end
        end,
    }
    StaticPopup_Show("RANDOMIZER_START_CONFIRM")
end

aloneButton:SetScript("OnClick", function()
    local where = chosenZone == 0 and "where you are now" or ("in " .. ZoneName(chosenZone, true))
    Confirm("Start alone " .. where .. ": this character will never make or join a co-op party.",
        "STARTSOLO\t" .. chosenZone)
end)
local UpdateGroup
partyButton:SetScript("OnClick", function()
    if partyBox:IsShown() then partyBox:Hide() else partyBox:Show() end
    UpdateGroup()
end)
createButton:SetScript("OnClick", function()
    local where = chosenZone == 0 and "a random start zone" or ZoneName(chosenZone, true)
    Confirm("Make a co-op party starting in " .. where .. ". You get a code to give your friends.",
        "STARTCREATE\t" .. chosenZone)
end)
joinButton:SetScript("OnClick", function()
    local code = joinBox:GetText():gsub("%s", ""):upper()
    if code == "" then return end
    joinBox:ClearFocus()
    Confirm("Join co-op party " .. code .. ": you start where that party starts.", "STARTJOIN\t" .. code)
end)
hardcoreButton:SetScript("OnClick", function()
    Confirm("Go hardcore: one life. If this character dies, it can't be played again (a GM can bring it back).",
        "HARDCORE", "START")
end)
joinBox:SetScript("OnEnterPressed", function() joinButton:Click() end)
joinBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)

-- Party members
local partyTitle = groupPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
partyTitle:SetJustifyH("LEFT")

local partyRows = {}
for i = 1, PARTY_ROWS do
    local row = CreateFrame("Frame", nil, groupPanel)
    row:SetWidth(670); row:SetHeight(34)
    row.bg = row:CreateTexture(nil, "BACKGROUND")
    row.bg:SetAllPoints()
    row.bg:SetTexture(0, 0, 0, 0.35)
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetWidth(26); row.icon:SetHeight(26)
    row.icon:SetPoint("LEFT", 4, 0)
    row.icon:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes")
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, 1)
    row.name:SetWidth(150); row.name:SetJustifyH("LEFT")
    row.what = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.what:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -2)
    row.what:SetWidth(150); row.what:SetJustifyH("LEFT")
    row.zone = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.zone:SetPoint("TOPLEFT", 200, -4)
    row.zone:SetWidth(170); row.zone:SetJustifyH("LEFT")
    row.tags = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.tags:SetPoint("TOPLEFT", row.zone, "BOTTOMLEFT", 0, -2)
    row.tags:SetWidth(170); row.tags:SetJustifyH("LEFT")
    row.roll = row:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
    row.roll:SetPoint("LEFT", 380, 0)
    row.roll:SetWidth(284); row.roll:SetHeight(30); row.roll:SetJustifyH("LEFT")
    row:Hide()
    partyRows[i] = row
end

local partyMore = groupPanel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")

local function StatusText()
    local c = challenge
    local hc
    if c.mode == 0 then hc = "Hardcore: |cff999999off on this server|r"
    elseif c.hardcore == 1 then hc = "|cffFF2020Hardcore: one life|r (alive)"
    elseif c.hardcore == 2 then hc = "|cffFF2020Hardcore: died|r"
    elseif c.mode == 2 then hc = "Hardcore: |cff999999not this character (made before it was on)|r"
    else hc = "Hardcore: |cff999999no|r" .. (c.canTry and " (you can still choose it)" or "") end
    local counts = c.counts == nil and "" or (c.counts and "   Achievements: |cff4CFF00this character counts|r" or
        "   Achievements: |cffFF6060this character doesn't count|r (choose how it starts, tie a roll or go hardcore)")
    return hc .. counts
end

local function StartText()
    local s = start
    if s.state == nil then return "Asking the server..." end
    if s.state == 0 then return "Co-op starts are off on this server." end
    if s.state == 2 then return "This character started |cffFFD100alone|r. It can't make or join a co-op party." end
    if s.state == 3 then
        return string.format("Co-op party |cffFFFF00%s|r - %d/%d players. Friends join with " ..
            "|cffFFFF00.coop join %s|r or this tab (up to level %d).", s.code, s.members, s.maxSize, s.code, s.maxLevel)
    end
    if s.canChoose then
        return "How does this character start? |cffFF6060You can only choose once.|r"
    end
    return "Not in a co-op party. " .. (s.why ~= "" and s.why or "")
end

UpdateGroup = function()
    if not frame:IsShown() or currentTab ~= 2 then return end
    groupStatus:SetText(StartText())
    statusLine:SetText(StatusText())
    Shown(hardcoreButton, challenge.canTry and challenge.hardcore == 0)
    groupErrorText:SetText(groupError or "")

    local choosing = start.canChoose and true or false
    Shown(chooser, choosing)
    if choosing then UIDropDownMenu_SetText(zoneMenu, ZoneName(chosenZone)) end
    Shown(zoneMenu, #startZones > 0)
    Shown(zoneLabel, #startZones > 0)
    Shown(zoneHint, #startZones > 0)
    if waitingChoice then
        aloneButton:Disable(); partyButton:Disable(); createButton:Disable(); joinButton:Disable()
    else
        aloneButton:Enable(); partyButton:Enable(); createButton:Enable(); joinButton:Enable()
    end

    partyTitle:ClearAllPoints()
    local top = choosing and (partyBox:IsShown() and -46 - 184 or -46 - 104) or -50
    partyTitle:SetPoint("TOPLEFT", 0, top)
    local others = 0
    for _, m in ipairs(party) do
        if m.flags % 16 < 8 then others = others + 1 end
    end
    if others == 0 then
        partyTitle:SetText("Your party |cff999999- you're not in a co-op party or a group yet.|r")
    else
        partyTitle:SetText("Your party")
    end

    for i = 1, PARTY_ROWS do
        local row, m = partyRows[i], party[i]
        if m then
            row:ClearAllPoints()
            row:SetPoint("TOPLEFT", partyTitle, "BOTTOMLEFT", 0, -6 - (i - 1) * 36)
            local file = CLASS_FILES[m.cls]
            local coords = file and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[file]
            if coords then row.icon:SetTexCoord(unpack(coords)); row.icon:Show() else row.icon:Hide() end
            row.icon:SetDesaturated(not m.online)
            local color = file and RAID_CLASS_COLORS and RAID_CLASS_COLORS[file]
            row.name:SetText(m.name)
            if not m.online then
                row.name:SetTextColor(0.5, 0.5, 0.5)
            elseif color then
                row.name:SetTextColor(color.r, color.g, color.b)
            else
                row.name:SetTextColor(1, 0.82, 0)
            end
            row.what:SetText(string.format("Level %d %s %s", m.level, RACE_NAMES[m.race] or "?",
                CLASS_NAMES[m.cls] or "?"))
            row.zone:SetText(m.online and (m.zone ~= "" and m.zone or "?") or "|cff999999Offline|r")
            local tags = {}
            for _, tag in ipairs(ROW_TAGS) do
                if m.flags % (tag[1] * 2) >= tag[1] then table.insert(tags, tag[2]) end
            end
            if m.flags % 4 >= 2 then table.insert(tags, "Co-op") end
            if m.flags % 8 >= 4 then table.insert(tags, "Group") end
            row.tags:SetText(table.concat(tags, ", "))
            row.roll:SetText(m.roll ~= "" and ("Roll: " .. m.roll) or (m.online and "|cff999999No roll|r" or ""))
            row:Show()
        else
            row:Hide()
        end
    end
    partyMore:ClearAllPoints()
    partyMore:SetPoint("TOPLEFT", partyTitle, "BOTTOMLEFT", 0, -6 - PARTY_ROWS * 36)
    partyMore:SetText(#party > PARTY_ROWS and ("and " .. (#party - PARTY_ROWS) .. " more") or "")
end

RequestGroup = function()
    Send("START")
    Send("PARTY")
    UpdateGroup()
end

-- ---------------------------------------------------------------- world tab (every zone and its bracket)

local worldPanel = CreateFrame("Frame", nil, frame)
worldPanel:SetPoint("TOPLEFT", 22, -46)
worldPanel:SetPoint("BOTTOMRIGHT", -22, 46)
worldPanel:Hide()

local worldHelp = worldPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
worldHelp:SetPoint("TOPLEFT", 0, -4)
worldHelp:SetWidth(670)
worldHelp:SetJustifyH("LEFT")
worldHelp:SetText("Every zone in the level bracket the shuffle gave it, lowest first. The levels in grey are the " ..
    "zone's own from before the shuffle. Yours is marked.")

local worldScroll = CreateFrame("ScrollFrame", "RandomizerWorldScroll", worldPanel, "UIPanelScrollFrameTemplate")
worldScroll:SetPoint("TOPLEFT", 0, -24)
worldScroll:SetPoint("BOTTOMRIGHT", -26, 0)
local worldChild = CreateFrame("Frame", nil, worldScroll)
worldChild:SetWidth(640); worldChild:SetHeight(10)
worldScroll:SetScrollChild(worldChild)
local worldText = worldChild:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
worldText:SetPoint("TOPLEFT", 4, 0)
worldText:SetWidth(630)
worldText:SetJustifyH("LEFT")
worldText:SetSpacing(2)

local worldZones, worldIncoming = {}, {}
local worldHere, worldOn = 0, nil
local TERRITORY = { [0] = "|cff4A9EFFAlliance|r", [1] = "|cffFF4A4AHorde|r", [2] = "|cffAAAAAAContested|r" }

local function UpdateWorld()
    if not frame:IsShown() or currentTab ~= 3 then return end
    if worldOn == nil then worldText:SetText("Asking the server...")
    elseif not worldOn then worldText:SetText("The world shuffle is off on this server: every zone keeps its levels.")
    else
        local lines, last = {}, nil
        for _, z in ipairs(worldZones) do
            if z.bracket ~= last then
                if last then table.insert(lines, " ") end
                table.insert(lines, "|cffFFD100Levels " .. z.bracket .. "|r")
                last = z.bracket
            end
            local here = z.id == worldHere and "  |cff4CFF00<< you are here|r" or ""
            table.insert(lines, string.format("    %s  |cff999999(was %s)|r  %s%s", z.name, z.was,
                TERRITORY[z.territory] or "", here))
        end
        worldText:SetText(table.concat(lines, "\n"))
    end
    worldChild:SetHeight(math.max(10, worldText:GetHeight() + 8))
end

local function OnWorldMessage(parts)
    if parts[1] == "W" then
        table.insert(worldIncoming, { id = tonumber(parts[2]) or 0, name = parts[3] or "?", bracket = parts[4] or "?",
            was = parts[5] or "?", territory = tonumber(parts[6]) or 2 })
        return true
    elseif parts[1] == "E" and parts[2] == "ZONES" then
        worldZones, worldIncoming = worldIncoming, {}
        worldHere = tonumber(parts[3]) or 0
        worldOn = parts[4] == "1"
        UpdateWorld()
        return true
    end
    return false
end

-- Tabs under the window
local tabs = {}
SelectTab = function(index)
    currentTab = index
    PanelTemplates_SetTab(frame, index)
    for _, widget in ipairs(achWidgets) do widget:Hide() end
    groupPanel:Hide()
    worldPanel:Hide()
    if index == 1 then
        scroll:Show(); whoLabel:Show(); summary:Show()
        title:SetText("Randomizer Achievements")
        if defsLoaded then Request(viewing) end
        Redraw()
    elseif index == 2 then
        groupPanel:Show()
        title:SetText("Randomizer - Group")
        RequestGroup()
    else
        worldPanel:Show()
        title:SetText("Randomizer - World")
        Send("ZONES")
        UpdateWorld()
    end
end

for index, text in ipairs({ "Achievements", "Group", "World" }) do
    local tab = CreateFrame("Button", "RandomizerAchievementsFrameTab" .. index, frame,
        "CharacterFrameTabButtonTemplate")
    tab:SetID(index)
    tab:SetText(text)
    if index == 1 then
        tab:SetPoint("TOPLEFT", frame, "BOTTOMLEFT", 16, 8)
    else
        tab:SetPoint("LEFT", tabs[index - 1], "RIGHT", -14, 0)
    end
    tab:SetScript("OnClick", function() SelectTab(index) end)
    tabs[index] = tab
end
PanelTemplates_SetNumTabs(frame, #tabs)
PanelTemplates_SetTab(frame, 1)

-- G / Z / M / E START / E PARTY from the server; true when it was one of them
local function OnGroupMessage(parts)
    local kind = parts[1]
    if kind == "G" then
        start.state = tonumber(parts[2]) or 0
        start.code = parts[3] or ""
        start.members = tonumber(parts[4]) or 0
        start.maxSize = tonumber(parts[5]) or 5
        start.maxLevel = tonumber(parts[6]) or 1
        start.canChoose = parts[7] == "1"
        start.why = parts[8] or ""
        startZonesIncoming = {}
        return true
    elseif kind == "Z" then
        table.insert(startZonesIncoming, { id = tonumber(parts[2]) or 0, name = parts[3] or "?", bracket = parts[4],
            was = parts[5] or "" })
        return true
    elseif kind == "H" then
        challenge.hardcore = tonumber(parts[2]) or 0
        challenge.counts = parts[3] == "1"
        challenge.mode = tonumber(parts[4]) or 0
        challenge.canTry = parts[5] == "1"
        return true
    elseif kind == "M" then
        table.insert(partyIncoming, { name = parts[2] or "?", online = parts[3] == "1",
            level = tonumber(parts[4]) or 0, race = tonumber(parts[5]) or 0, cls = tonumber(parts[6]) or 0,
            zone = parts[7] or "", flags = tonumber(parts[8]) or 0, roll = parts[9] or "" })
        return true
    elseif kind == "E" and parts[2] == "START" then
        startZones = startZonesIncoming
        if waitingChoice then
            waitingChoice = nil
            Send("PARTY") -- a new party, or a new member: show it
        end
        -- Not chosen yet: the chooser opens by itself once per session
        if start.canChoose and not startPrompted then
            startPrompted = true
            Print("This character hasn't chosen how it starts: alone or with a co-op party (the Group tab).")
            frame:Show()
            SelectTab(2)
        end
        UpdateGroup()
        return true
    elseif kind == "E" and parts[2] == "PARTY" then
        party = partyIncoming
        partyIncoming = {}
        UpdateGroup()
        return true
    elseif kind == "X" and waitingChoice then
        groupError = parts[2] or ""
        Print(groupError)
        return true
    end
    return false
end

-- ---------------------------------------------------------------- achievement popup (like the game's own alert)

local POPUP_TIME = 4.5   -- seconds fully shown, then it fades out
local POPUP_FADE = 1.5
local popups, popupQueue = {}, {}

local function CreatePopup(index)
    local p = CreateFrame("Button", "RandomizerAchievementAlert" .. index, UIParent)
    p:SetWidth(300); p:SetHeight(88)
    p:SetFrameStrata("DIALOG")
    p:Hide()
    p.busy, p.age = false, 0

    p.bg = p:CreateTexture(nil, "BACKGROUND")
    p.bg:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Alert")
    p.bg:SetTexCoord(0, 0.605, 0, 0.703)
    p.bg:SetAllPoints()

    p.icon = p:CreateTexture(nil, "BORDER")
    p.icon:SetWidth(50); p.icon:SetHeight(50)
    p.icon:SetPoint("LEFT", 26, 2)
    p.iconFrame = p:CreateTexture(nil, "ARTWORK")
    p.iconFrame:SetTexture("Interface\\AchievementFrame\\UI-Achievement-IconFrame")
    p.iconFrame:SetTexCoord(0, 0.5625, 0, 0.5625)
    p.iconFrame:SetWidth(72); p.iconFrame:SetHeight(72)
    p.iconFrame:SetPoint("CENTER", p.icon, "CENTER", -1, 2)

    p.unlocked = p:CreateFontString(nil, "OVERLAY", "GameFontBlackTiny")
    p.unlocked:SetPoint("TOP", 7, -23)
    p.unlocked:SetText("Randomizer Achievement")

    p.name = p:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    p.name:SetPoint("TOP", p.unlocked, "BOTTOM", 0, -4)
    p.name:SetWidth(190); p.name:SetHeight(16)

    p.reward = p:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
    p.reward:SetPoint("TOP", p.name, "BOTTOM", 0, -1)
    p.reward:SetWidth(200); p.reward:SetHeight(24)
    p.reward:SetJustifyV("TOP")

    p.glow = p:CreateTexture(nil, "OVERLAY")
    p.glow:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Alert-Glow")
    p.glow:SetBlendMode("ADD")
    p.glow:SetTexCoord(0, 0.78125, 0, 0.66796875)
    p.glow:SetWidth(400); p.glow:SetHeight(171)
    p.glow:SetPoint("CENTER")

    p.shine = p:CreateTexture(nil, "OVERLAY")
    p.shine:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Alert-Glow")
    p.shine:SetBlendMode("ADD")
    p.shine:SetTexCoord(0.78125, 0.912109375, 0, 0.28125)
    p.shine:SetWidth(67); p.shine:SetHeight(72)

    -- Glow flash, a shine sweeping across, a few seconds shown, then a fade; hovering keeps it
    p:SetScript("OnUpdate", function(self, elapsed)
        self.age = self.age + elapsed
        local t = self.age
        self.glow:SetAlpha(t < 0.2 and t / 0.2 or math.max(0, 1 - (t - 0.2) / 0.5))
        if t < 0.85 then
            self.shine:Show()
            self.shine:SetPoint("TOPLEFT", self, "TOPLEFT", -10 + 270 * (t / 0.85), -8)
            self.shine:SetAlpha(t < 0.15 and t / 0.15 or 1 - t / 0.85)
        else
            self.shine:Hide()
        end
        if MouseIsOver(self) and t > 0.85 then self.age = math.min(self.age, POPUP_TIME) end
        if t > POPUP_TIME then
            local alpha = 1 - (t - POPUP_TIME) / POPUP_FADE
            if alpha <= 0 then
                self:Hide()
                return
            end
            self:SetAlpha(alpha)
        end
    end)
    p:SetScript("OnHide", function(self)
        self.busy = false
        local nextOne = table.remove(popupQueue, 1)
        if nextOne then ShowPopupNow(nextOne) end
    end)
    p:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    p:SetScript("OnClick", function(self, button)
        self:Hide()
        if button == "LeftButton" and available then
            viewing = UnitName("player")
            frame:Show()
            SelectTab(1)
        end
    end)
    p:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP")
        GameTooltip:AddLine(self.achName or "", 1, 0.82, 0)
        if self.achReward and self.achReward ~= "" then GameTooltip:AddLine(self.achReward, 0.1, 1, 0.1, true) end
        GameTooltip:AddLine("Click: open the achievements. Right-click: close.", 0.6, 0.8, 1)
        GameTooltip:Show()
    end)
    p:SetScript("OnLeave", function() GameTooltip:Hide() end)

    -- Where the game puts its own alerts: above the action bars, the second one above the first
    p:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 128 + (index - 1) * 92)
    popups[index] = p
    return p
end

ShowPopupNow = function(ach)
    for index = 1, 2 do
        local p = popups[index] or CreatePopup(index)
        if not p.busy then
            p.busy = true
            p.age = 0
            p.achName, p.achReward = ach.name, ach.reward
            p:SetAlpha(1)
            p.icon:SetTexture("Interface\\Icons\\" .. (ach.icon ~= "" and ach.icon or "INV_Misc_QuestionMark"))
            p.name:SetText(ach.name)
            p.reward:SetText(ach.reward or "")
            p:Show()
            if not UI_DB_POPUP().popupMute then PlaySoundFile("Sound\\Spells\\AchievmentSound1.wav") end
            return
        end
    end
    table.insert(popupQueue, ach)
end

-- "A" from the server: an achievement was just earned
local function OnEarned(parts)
    local ach = { id = tonumber(parts[2]) or 0, icon = parts[3] or "", name = parts[4] or "", reward = parts[5] or "" }
    if not UI_DB_POPUP().noPopup then ShowPopupNow(ach) end
    if frame:IsShown() and currentTab == 1 then Request(viewing) end
end


-- Version check: the server tells us the addon version it expects ("V"). If ours differs, say so once per login in
-- a popup with the download link (WoW can't open links, so it's in a box to copy). The releases are tagged v<version>.
local RELEASES_URL = "https://github.com/TheSCREWEDSoftware/mod-world-randomizer/releases"
local versionWarned
local function CheckVersion()
    if versionWarned or not serverVersion or serverVersion == ADDON_VERSION then return end
    versionWarned = true
    local older = ADDON_VERSION < serverVersion
    local url = older and (RELEASES_URL .. "/tag/v" .. serverVersion) or RELEASES_URL
    local text
    if older then
        text = "Randomizer Achievements is out of date: you have version " .. ADDON_VERSION ..
            ", this server uses version " .. serverVersion .. ".\n\nDownload version " .. serverVersion ..
            " (select the link and press Ctrl+C):"
    else
        text = "This server runs an older version of the module than your Randomizer Achievements addon (server " ..
            serverVersion .. ", addon " .. ADDON_VERSION .. "). Ask the server's admin to update it, or use " ..
            "addon version " .. serverVersion .. " from the releases:"
    end
    Print(text:gsub("\n\n", " ") .. " " .. url)
    StaticPopupDialogs["RANDACH_VERSION"] = {
        text = text, button1 = OKAY, hasEditBox = 1, timeout = 0, whileDead = true, hideOnEscape = true,
        OnShow = function(self)
            local box = _G[self:GetName() .. "EditBox"]
            box:SetWidth(330)
            box:SetText(url)
            box:HighlightText()
            box:SetFocus()
        end,
        EditBoxOnEnterPressed = function(self) self:GetParent():Hide() end,
        EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
        -- Keep the link intact if the box is typed in
        EditBoxOnTextChanged = function(self)
            if self:GetText() ~= url then self:SetText(url); self:HighlightText() end
        end,
    }
    StaticPopup_Show("RANDACH_VERSION")
end

local function OnMessage(message)
    local parts = Split(message, "\t")
    local kind = parts[1]
    if available ~= true then
        local wasHidden = available == false
        available = true
        replyTimeout = nil
        if wasHidden and Revive then Revive() end
    end
    if OnRollMessage(parts) or OnGroupMessage(parts) or OnWorldMessage(parts) then return end

    if kind == "A" then
        OnEarned(parts)
    elseif kind == "V" then
        serverVersion = tonumber(parts[2])
        CheckVersion()
    elseif kind == "L" then
        -- The server gives talent points before level 10: open the talent frame from that level
        local level = tonumber(parts[2])
        if level and level >= 1 and level < 10 then
            SHOW_TALENT_LEVEL = level
            if UpdateMicroButtons then UpdateMicroButtons() end
        end
    elseif kind == "C" then
        local id = tonumber(parts[2])
        for _, cat in ipairs(categories) do
            if cat[1] == id then return end
        end
        table.insert(categories, { id, parts[3] or "" })
    elseif kind == "D" then
        local id = tonumber(parts[2])
        if not defs[id] then table.insert(order, id) end
        defs[id] = { cat = tonumber(parts[3]), icon = parts[4] or "", name = parts[5] or "", reward = parts[6] or "",
            exp = tonumber(parts[7]) or 0, parent = tonumber(parts[8]) or 0, desc = defs[id] and defs[id].desc or "" }
    elseif kind == "T" then
        local id = tonumber(parts[2])
        if defs[id] then defs[id].desc = parts[3] or "" end
    elseif kind == "S" then
        local name = parts[2]
        incoming[name] = { stats = {
            done = tonumber(parts[3]) or 0, total = tonumber(parts[4]) or 0, deaths = tonumber(parts[5]) or 0,
            zones = tonumber(parts[6]) or 0, quests = tonumber(parts[7]) or 0, rares = tonumber(parts[8]) or 0,
            gold = tonumber(parts[9]) or 0, cap = tonumber(parts[10]) or 0 }, ach = {} }
        incoming.current = name
    elseif kind == "P" then
        local target = incoming[incoming.current or ""]
        if not target then return end
        for _, entry in ipairs(Split(parts[2] or "", ",")) do
            local id, done, prog, req = string.match(entry, "(%d+):(%d):(%d+):(%d+)")
            if id then
                target.ach[tonumber(id)] = { done = done == "1", prog = tonumber(prog), req = tonumber(req) }
            end
        end
    elseif kind == "E" then
        if parts[2] == "DEF" then
            defsLoaded = true
            expansion = tonumber(parts[4]) or 3
            Request(viewing)
        elseif parts[2] == "GET" then
            local name = parts[3]
            if incoming[name] then
                progress[name] = incoming[name]
                incoming[name] = nil
            end
            Redraw()
        end
    elseif kind == "X" then
        local off = parts[2] and parts[2]:find("Achievements are off")
        if not (off and achievementsOff) then Print(parts[2] or "") end
        if off then achievementsOff = true end
        summary:SetText(parts[2] or "")
        -- The whole module off: the addon turns itself off. Achievements off: "My roll" still works
        if parts[2] and parts[2]:find("World Randomizer is off") then Disable() end
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:RegisterEvent("RAID_ROSTER_UPDATE")

-- The server said the World Randomizer is off: stay quiet until the next login
Disable = function()
    available = false
    moduleOff = true
    replyTimeout = nil
    frame:Hide()
    if minimapButton then minimapButton:Hide() end
    events:UnregisterEvent("CHAT_MSG_ADDON")
    events:UnregisterEvent("PARTY_MEMBERS_CHANGED")
    events:UnregisterEvent("RAID_ROSTER_UPDATE")
end

-- Ask the server (again): the first answer turns everything on. Right after a login or a server restart the
-- answer can take a while, so it asks a few times before hiding, and keeps listening after that.
local function Ask()
    if moduleOff then return end
    askTries = askTries + 1
    replyTimeout = 10
    Send("DEF\t" .. ADDON_VERSION)
    Send("START") -- not chosen yet: the Group tab opens with the chooser
end

-- No answer after every try: hide the window and the minimap button, but an answer later still brings them back
local function SoftHide()
    available = false
    replyTimeout = nil
    frame:Hide()
    if minimapButton then minimapButton:Hide() end
    Print("No answer from the server yet (it may not run the World Randomizer, or it is still starting). " ..
        "Type /randach to ask again.")
end

local loginDelay
events:SetScript("OnEvent", function(self, event, prefix, message, channel, sender)
    if event == "CHAT_MSG_ADDON" then
        if prefix == PREFIX and channel == "WHISPER" and sender == UnitName("player") then
            OnMessage(message)
        end
    elseif event == "PLAYER_LOGIN" then
        viewing = UnitName("player")
        loginDelay = 3 -- seconds, so the server has finished logging us in
    elseif event == "PLAYER_ENTERING_WORLD" then
        -- After a loading screen: if the server never answered, ask again
        if available ~= true and not loginDelay and not replyTimeout and not moduleOff then
            askTries = 0
            Ask()
        end
    elseif frame:IsShown() then
        if currentTab == 2 then Send("PARTY") else UpdateMembers() end
    end
end)

events:SetScript("OnUpdate", function(self, elapsed)
    -- Progress changes while you play: the open window asks again every few seconds
    if frame:IsShown() and available and currentTab ~= 3 and (currentTab == 2 or not achievementsOff) then
        autoRefresh = autoRefresh - elapsed
        if autoRefresh <= 0 then
            autoRefresh = 5
            if currentTab == 2 then Send("PARTY") else Request(viewing) end
        end
    end

    if loginDelay then
        loginDelay = loginDelay - elapsed
        if loginDelay <= 0 then
            loginDelay = nil
            askTries = 0
            Ask()
        end
    elseif replyTimeout then
        if available then
            replyTimeout = nil
            return
        end
        replyTimeout = replyTimeout - elapsed
        if replyTimeout <= 0 then
            if askTries < ASK_TRIES then Ask() else SoftHide() end
        end
    end
end)

frame:SetScript("OnShow", function()
    if available == false then frame:Hide() return end
    autoRefresh = 5
    if not defsLoaded then Send("DEF\t" .. ADDON_VERSION) end
    SelectTab(currentTab)
end)

SLASH_RANDACH1 = "/randach"
SlashCmdList["RANDACH"] = function(arg)
    arg = arg and arg:gsub("^%s+", ""):gsub("%s+$", "") or ""
    if moduleOff then
        Print("Not available: the World Randomizer is off on this server.")
        return
    elseif available == false then
        Print("Asking the server again...")
        available = nil
        askTries = ASK_TRIES - 1 -- one more try
        Ask()
        return
    elseif available == nil then
        Print("Still waiting for the server to answer, try again in a few seconds.")
        return
    end
    if arg:lower() == "roll" then
        if rollFrame:IsShown() then rollFrame:Hide() else rollFrame:Show() end
    elseif arg:lower() == "group" then
        frame:Show()
        SelectTab(2)
    elseif arg:lower() == "world" or arg:lower() == "zones" then
        frame:Show()
        SelectTab(3)
    elseif arg ~= "" then
        viewing = arg:sub(1, 1):upper() .. arg:sub(2):lower()
        frame:Show()
        SelectTab(1)
    elseif frame:IsShown() then
        frame:Hide()
    else
        viewing = UnitName("player")
        frame:Show()
    end
end

local UI_NAME = "RandomizerAchievements"
local UI_TITLE = "Randomizer Achievements"
local UI_ICON = "Interface\\Icons\\Achievement_General"
local UI_DEFAULT_ANGLE = 200
local UI_CLICK_TEXT = "open the achievements"
local UI_HELP = "Account-wide achievements of mod-world-randomizer, with their rewards, for you and your group.\n\n" ..
    "A popup shows each achievement you earn, like the game's own (Interface > AddOns turns it off).\n\n" ..
    "/randach - show or hide the window\n/randach <name> - a group member's achievements\n/randach group - " ..
    "start alone or with a co-op party, and your party's info\n/randach world - every zone and its levels\n" ..
    "/randach roll - tie this character to its roll " ..
    "from the roller page\n.rach - your rewards in chat\n\nThe window updates by " ..
    "itself every few seconds while it is open."
local function UI_DB()
    RandomizerAchievementsDB = RandomizerAchievementsDB or {}
    return RandomizerAchievementsDB
end
local function UI_STATUS()
    if available then
        local versionText = " Addon version " .. ADDON_VERSION .. "."
        if serverVersion and serverVersion ~= ADDON_VERSION then
            versionText = " |cffFF6060Addon version " .. ADDON_VERSION .. ", the server expects " .. serverVersion ..
                ": update the addon.|r"
        end
        return "|cff4CFF00Connected:|r the server runs the World Randomizer." .. versionText
    end
    if moduleOff then return "|cffFF6060Not available:|r the World Randomizer is off on this server." end
    if available == false then
        return "|cffFF6060No answer from the server|r (it may not run the World Randomizer). /randach asks again."
    end
    return "Waiting for the server to answer..."
end
local function UI_TOGGLE() SlashCmdList["RANDACH"]("") end

-- ---------------------------------------------------------------- minimap button and Interface > AddOns panel

local function PlaceMinimapButton()
    local angle = math.rad(UI_DB().minimapAngle or UI_DEFAULT_ANGLE)
    minimapButton:ClearAllPoints()
    minimapButton:SetPoint("CENTER", Minimap, "CENTER", math.cos(angle) * 80, math.sin(angle) * 80)
end

local function UpdateMinimapButton()
    if not minimapButton then return end
    if available == false or UI_DB().hideMinimap then minimapButton:Hide() else minimapButton:Show() end
end

-- The server answered after the addon had hidden: show everything again
Revive = function()
    UpdateMinimapButton()
    Print("The server answered: the addon is on.")
end

local function CreateMinimapButton()
    minimapButton = CreateFrame("Button", UI_NAME .. "MinimapButton", Minimap)
    minimapButton:SetWidth(31); minimapButton:SetHeight(31)
    minimapButton:SetFrameStrata("MEDIUM")
    minimapButton:SetFrameLevel(8)
    minimapButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    minimapButton:RegisterForDrag("LeftButton")
    minimapButton:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

    local background = minimapButton:CreateTexture(nil, "BACKGROUND")
    background:SetWidth(20); background:SetHeight(20)
    background:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
    background:SetPoint("TOPLEFT", 7, -5)
    local icon = minimapButton:CreateTexture(nil, "ARTWORK")
    icon:SetWidth(17); icon:SetHeight(17)
    icon:SetTexture(UI_ICON)
    icon:SetPoint("TOPLEFT", 7, -6)
    local border = minimapButton:CreateTexture(nil, "OVERLAY")
    border:SetWidth(53); border:SetHeight(53)
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    border:SetPoint("TOPLEFT")

    -- Drag it around the minimap
    minimapButton:SetScript("OnDragStart", function(self)
        self:SetScript("OnUpdate", function()
            local mx, my = Minimap:GetCenter()
            local x, y = GetCursorPosition()
            local scale = Minimap:GetEffectiveScale()
            UI_DB().minimapAngle = math.deg(math.atan2(y / scale - my, x / scale - mx))
            PlaceMinimapButton()
        end)
    end)
    minimapButton:SetScript("OnDragStop", function(self) self:SetScript("OnUpdate", nil) end)
    minimapButton:SetScript("OnClick", function(self, button)
        if button == "RightButton" then
            InterfaceOptionsFrame_OpenToCategory(optionsPanel)
        else
            UI_TOGGLE()
        end
    end)
    minimapButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:AddLine(UI_TITLE)
        GameTooltip:AddLine(UI_STATUS(), 1, 1, 1, true)
        GameTooltip:AddLine("Left-click: " .. UI_CLICK_TEXT, 0.6, 0.8, 1)
        GameTooltip:AddLine("Right-click: options. Drag: move this button.", 0.6, 0.8, 1)
        GameTooltip:Show()
    end)
    minimapButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

    PlaceMinimapButton()
    UpdateMinimapButton()
end

optionsPanel = CreateFrame("Frame", UI_NAME .. "Options", UIParent)
optionsPanel.name = UI_TITLE
optionsPanel:Hide()

local optionsTitle = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
optionsTitle:SetPoint("TOPLEFT", 16, -16)
optionsTitle:SetText(UI_TITLE)

local optionsText = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
optionsText:SetPoint("TOPLEFT", optionsTitle, "BOTTOMLEFT", 0, -8)
optionsText:SetWidth(560)
optionsText:SetJustifyH("LEFT")

local optionsStatus = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontNormal")
optionsStatus:SetPoint("TOPLEFT", optionsText, "BOTTOMLEFT", 0, -12)
optionsStatus:SetWidth(560)
optionsStatus:SetJustifyH("LEFT")

local minimapCheck = CreateFrame("CheckButton", UI_NAME .. "MinimapCheck", optionsPanel,
    "InterfaceOptionsCheckButtonTemplate")
minimapCheck:SetPoint("TOPLEFT", optionsStatus, "BOTTOMLEFT", -2, -12)
_G[UI_NAME .. "MinimapCheckText"]:SetText("Show the minimap button")
minimapCheck:SetScript("OnClick", function(self)
    UI_DB().hideMinimap = not self:GetChecked()
    UpdateMinimapButton()
end)

local popupCheck = CreateFrame("CheckButton", UI_NAME .. "PopupCheck", optionsPanel,
    "InterfaceOptionsCheckButtonTemplate")
popupCheck:SetPoint("TOPLEFT", minimapCheck, "BOTTOMLEFT", 0, -4)
_G[UI_NAME .. "PopupCheckText"]:SetText("Show a popup when you earn an achievement (the chat line stays)")
popupCheck:SetScript("OnClick", function(self) UI_DB().noPopup = not self:GetChecked() end)

local soundCheck = CreateFrame("CheckButton", UI_NAME .. "SoundCheck", optionsPanel,
    "InterfaceOptionsCheckButtonTemplate")
soundCheck:SetPoint("TOPLEFT", popupCheck, "BOTTOMLEFT", 0, -4)
_G[UI_NAME .. "SoundCheckText"]:SetText("Play a sound with the popup")
soundCheck:SetScript("OnClick", function(self) UI_DB().popupMute = not self:GetChecked() end)

local previewButton = CreateFrame("Button", nil, optionsPanel, "UIPanelButtonTemplate")
previewButton:SetWidth(120); previewButton:SetHeight(22)
previewButton:SetPoint("LEFT", _G[UI_NAME .. "PopupCheckText"], "RIGHT", 12, 0)
previewButton:SetText("Preview")
previewButton:SetScript("OnClick", function()
    ShowPopupNow({ icon = "Achievement_General", name = "Into the Unknown", reward = "+5% experience" })
end)

local openButton = CreateFrame("Button", nil, optionsPanel, "UIPanelButtonTemplate")
openButton:SetWidth(160); openButton:SetHeight(22)
openButton:SetPoint("TOPLEFT", soundCheck, "BOTTOMLEFT", 2, -12)
openButton:SetText(UI_CLICK_TEXT:sub(1, 1):upper() .. UI_CLICK_TEXT:sub(2))
openButton:SetScript("OnClick", function()
    InterfaceOptionsFrame:Hide()
    UI_TOGGLE()
end)

optionsPanel:SetScript("OnShow", function()
    optionsText:SetText(UI_HELP)
    optionsStatus:SetText(UI_STATUS())
    minimapCheck:SetChecked(not UI_DB().hideMinimap)
    popupCheck:SetChecked(not UI_DB().noPopup)
    soundCheck:SetChecked(not UI_DB().popupMute)
    if available then openButton:Enable() else openButton:Disable() end
end)

InterfaceOptions_AddCategory(optionsPanel)

local uiEvents = CreateFrame("Frame")
uiEvents:RegisterEvent("PLAYER_LOGIN")
uiEvents:SetScript("OnEvent", CreateMinimapButton)
