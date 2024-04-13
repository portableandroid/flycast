/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "gui.h"
#include "imgui.h"
#include "gui_util.h"
#include "cheats.h"
#ifdef __ANDROID__
#include "oslib/storage.h"
#endif

static bool addingCheat;

static void addCheat()
{
	static char cheatName[64];
	static char cheatCode[128];
    centerNextWindow();
    ImGui::SetNextWindowSize(min(ImGui::GetIO().DisplaySize, ScaledVec2(600.f, 400.f)));

    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar
    		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(20, 8));
    ImGui::AlignTextToFramePadding();
    ImGui::Indent(10 * settings.display.uiScale);
    ImGui::Text("ADD CHEAT");

	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("Cancel").x - ImGui::GetStyle().FramePadding.x * 4.f
    	- ImGui::CalcTextSize("OK").x - ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::Button("Cancel"))
		addingCheat = false;
	ImGui::SameLine();
	if (ImGui::Button("OK"))
	{
		try {
			cheatManager.addGameSharkCheat(cheatName, cheatCode);
			addingCheat = false;
			cheatName[0] = 0;
			cheatCode[0] = 0;
		} catch (const FlycastException& e) {
			gui_error(e.what());
		}
	}

    ImGui::Unindent(10 * settings.display.uiScale);
    ImGui::PopStyleVar();

	ImGui::BeginChild(ImGui::GetID("input"), ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_NavFlattened);
    {
		ImGui::InputText("Name", cheatName, sizeof(cheatName), 0, nullptr, nullptr);
		ImGui::InputTextMultiline("Code", cheatCode, sizeof(cheatCode), ImVec2(0, ImGui::GetTextLineHeight() * 8), 0, nullptr, nullptr);
    }
	ImGui::EndChild();
	ImGui::End();
}

static void cheatFileSelected(bool cancelled, std::string path)
{
	if (!cancelled)
		cheatManager.loadCheatFile(path);
}

void gui_cheats()
{
	if (addingCheat)
	{
		addCheat();
		return;
	}
    centerNextWindow();
    ImGui::SetNextWindowSize(min(ImGui::GetIO().DisplaySize, ScaledVec2(600.f, 400.f)));

    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar
    		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(20, 8));
    ImGui::AlignTextToFramePadding();
    ImGui::Indent(10 * settings.display.uiScale);
    ImGui::Text("CHEATS");

	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("Add").x  - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 6.f
    	- ImGui::CalcTextSize("Load").x - ImGui::GetStyle().ItemSpacing.x * 2);
	if (ImGui::Button("Add"))
		addingCheat = true;
	ImGui::SameLine();
#ifdef __ANDROID__
	if (ImGui::Button("Load"))
		hostfs::addStorage(false, true, cheatFileSelected);
#else
	if (ImGui::Button("Load"))
    	ImGui::OpenPopup("Select cheat file");
	select_file_popup("Select cheat file", [](bool cancelled, std::string selection)
		{
			cheatFileSelected(cancelled, selection);
			return true;
		}, true, "cht");
#endif

	ImGui::SameLine();
	if (ImGui::Button("Close"))
		gui_setState(GuiState::Commands);

    ImGui::Unindent(10 * settings.display.uiScale);
    ImGui::PopStyleVar();

	ImGui::BeginChild(ImGui::GetID("cheats"), ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_DragScrolling | ImGuiWindowFlags_NavFlattened);
    {
		if (cheatManager.cheatCount() == 0)
			ImGui::Text("(No cheat loaded)");
		else
			for (size_t i = 0; i < cheatManager.cheatCount(); i++)
			{
				ImGui::PushID(("cheat" + std::to_string(i)).c_str());
				bool v = cheatManager.cheatEnabled(i);
				if (ImGui::Checkbox(cheatManager.cheatDescription(i).c_str(), &v))
					cheatManager.enableCheat(i, v);
				ImGui::PopID();
			}
    }
    scrollWhenDraggingOnVoid();
    windowDragScroll();

	ImGui::EndChild();
	ImGui::End();
}
