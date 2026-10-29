-- Randomizer Achievements (WoW 3.3.5)
-- Talks to mod-world-randomizer with addon whispers to yourself (prefix RANDACH):
--   asks for the achievement list once, then for the progress of you or a party / raid member.
-- If the server doesn't answer within 10 seconds of logging in (no module, or achievements are off), the addon
-- turns itself off until the next login.
-- /randach            show or hide the window
-- /randach <name>     show a group member's achievements

local PREFIX = "RANDACH"
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
local available = nil    -- nil: waiting for the server, true: it answered, false: no module (off until next login)
local replyTimeout
local minimapButton, optionsPanel
local autoRefresh = 0    -- seconds until the open window asks the server again
local EXPANSION_NAMES = { "Vanilla", "TBC", "Wrath" }

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
    return button
end

-- Achievement rows
local scroll = CreateFrame("ScrollFrame", "RandomizerAchievementsScroll", frame, "FauxScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 200, -58)
scroll:SetWidth(470); scroll:SetHeight(ROWS * ROW_HEIGHT)

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
end

-- Whose achievements: you and your group
local memberButtons = {}
local refresh = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
refresh:SetWidth(80); refresh:SetHeight(22)
refresh:SetPoint("BOTTOMRIGHT", -20, 18)
refresh:SetText("Refresh")

local whoLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
whoLabel:SetPoint("BOTTOMLEFT", 22, 23)
whoLabel:SetText("Show:")

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
    summary:SetText(string.format("%d / %d done   Deaths %d   Zones %d   This run: %d quests, %d rares, %dg%s",
        s.done, s.total, s.deaths, s.zones, s.quests, s.rares, s.gold, cap))
end

local function Redraw()
    if not frame:IsShown() then return end
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

refresh:SetScript("OnClick", function() Request(viewing) end)

local Disable

local function Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffFFD100[Randomizer Achievements]|r " .. text)
end

local function OnMessage(message)
    local parts = Split(message, "\t")
    local kind = parts[1]
    available = true

    if kind == "L" then
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
        Print(parts[2] or "")
        summary:SetText(parts[2] or "")
        if parts[2] and parts[2]:find("off on this server") then Disable() end
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:RegisterEvent("RAID_ROSTER_UPDATE")

-- No module on this server (or achievements are off): stay quiet until the next login
Disable = function()
    available = false
    replyTimeout = nil
    frame:Hide()
    if minimapButton then minimapButton:Hide() end
    events:UnregisterEvent("CHAT_MSG_ADDON")
    events:UnregisterEvent("PARTY_MEMBERS_CHANGED")
    events:UnregisterEvent("RAID_ROSTER_UPDATE")
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
    else
        if frame:IsShown() then UpdateMembers() end
    end
end)

events:SetScript("OnUpdate", function(self, elapsed)
    -- Progress changes while you play: the open window asks again every few seconds
    if frame:IsShown() and available then
        autoRefresh = autoRefresh - elapsed
        if autoRefresh <= 0 then
            autoRefresh = 5
            Request(viewing)
        end
    end

    if loginDelay then
        loginDelay = loginDelay - elapsed
        if loginDelay <= 0 then
            loginDelay = nil
            replyTimeout = 10
            Send("DEF")
        end
    elseif replyTimeout then
        if available then
            replyTimeout = nil
            return
        end
        replyTimeout = replyTimeout - elapsed
        if replyTimeout <= 0 then
            Disable()
            Print("This server doesn't run the World Randomizer achievements, so the addon is off until your next "
                .. "login.")
        end
    end
end)

frame:SetScript("OnShow", function()
    if available == false then frame:Hide() return end
    autoRefresh = 5
    if defsLoaded then Request(viewing) else Send("DEF") end
    Redraw()
end)

SLASH_RANDACH1 = "/randach"
SlashCmdList["RANDACH"] = function(arg)
    arg = arg and arg:gsub("^%s+", ""):gsub("%s+$", "") or ""
    if available == false then
        Print("Not available: this server doesn't run the World Randomizer achievements.")
        return
    elseif available == nil then
        Print("Still waiting for the server to answer, try again in a few seconds.")
        return
    end
    if arg ~= "" then
        viewing = arg:sub(1, 1):upper() .. arg:sub(2):lower()
        frame:Show()
        Request(viewing)
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
    "/randach - show or hide the window\n/randach <name> - a group member's achievements\n.rach - your rewards " ..
    "in chat\n\nThe window updates by itself every few seconds while it is open."
local function UI_DB()
    RandomizerAchievementsDB = RandomizerAchievementsDB or {}
    return RandomizerAchievementsDB
end
local function UI_STATUS()
    if available then return "|cff4CFF00Connected:|r the server runs the World Randomizer achievements." end
    if available == false then
        return "|cffFF6060Not available:|r this server doesn't run the World Randomizer achievements (off until " ..
            "your next login)."
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

local openButton = CreateFrame("Button", nil, optionsPanel, "UIPanelButtonTemplate")
openButton:SetWidth(160); openButton:SetHeight(22)
openButton:SetPoint("TOPLEFT", minimapCheck, "BOTTOMLEFT", 2, -12)
openButton:SetText(UI_CLICK_TEXT:sub(1, 1):upper() .. UI_CLICK_TEXT:sub(2))
openButton:SetScript("OnClick", function()
    InterfaceOptionsFrame:Hide()
    UI_TOGGLE()
end)

optionsPanel:SetScript("OnShow", function()
    optionsText:SetText(UI_HELP)
    optionsStatus:SetText(UI_STATUS())
    minimapCheck:SetChecked(not UI_DB().hideMinimap)
    if available then openButton:Enable() else openButton:Disable() end
end)

InterfaceOptions_AddCategory(optionsPanel)

local uiEvents = CreateFrame("Frame")
uiEvents:RegisterEvent("PLAYER_LOGIN")
uiEvents:SetScript("OnEvent", CreateMinimapButton)
