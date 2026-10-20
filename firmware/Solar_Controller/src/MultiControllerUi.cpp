#include "MultiControllerInternal.h"

static String mcHtmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '\"') out += F("&quot;");
    else out += c;
  }
  return out;
}

static String mcSelInt(int value, int expected) {
  return (value == expected) ? F(" selected") : String();
}

static void mcAppendYesNoSelect(String& page, const String& name, bool value, const String& label) {
  page += "<label>" + label + "</label>";
  page += "<select name='" + name + "'>";
  page += "<option value='1'" + mcSelInt(value ? 1 : 0, 1) + ">Ja</option>";
  page += "<option value='0'" + mcSelInt(value ? 1 : 0, 0) + ">Nee</option>";
  page += "</select>";
}

String multiControllerBuildSettingsSection() {
  String page;
  const uint8_t uiRole = mcEnabled ? mcRole : ((mcLastActiveRole == MC_ROLE_SLAVE) ? MC_ROLE_SLAVE : MC_ROLE_MASTER);
  String mcRoleTag = "standalone";
  if (mcEnabled && mcRole == MC_ROLE_MASTER) mcRoleTag = "master";
  else if (mcEnabled && mcRole == MC_ROLE_SLAVE) mcRoleTag = "slave";

  page += "<details class='acc'><summary><div class='sum'><b>Multi Controller</b><span class='tag'>" + mcRoleTag + "</span></div></summary><div class='accbd'>";
  page += "<div class='help'>Lokaal: max. 3 controllers totaal. PWM1 = <b>GPIO25</b>; extra PWM = <b>GPIO26/27/32</b>.</div>";
  page += "<div class='help'>Globale Solar Controller-instellingen blijven gelden als er geen per-controller instelling is.</div>";
  page += "<div class='help'>Safety fallback: <b>" + String(multiControllerIsFallbackActive() ? "actief" : "uit") + "</b> — reden: <b>" + mcHtmlEscape(multiControllerGetFallbackReason()) + "</b></div>";
  page += "<div class='help'>Config validatie: <b>" + String(multiControllerIsConfigValid() ? "OK" : "niet OK") + "</b> — Runtime validatie: <b>" + String(multiControllerIsRuntimeValid() ? "OK" : "niet OK") + "</b></div>";
  String mcRequestLabel = multiControllerScenarioUsesParallelPriorityTargets(mcCurrentScenario) ? "Groeps-cap nu (na safety/softstart)" : "Totale groeps-output doel";
  page += "<div class='help'>Lokale outputs gevraagd: <b>" + String(multiControllerGetRequestedLocalPwmCount()) + "</b> — actief: <b>" + String(multiControllerGetEffectiveLocalPwmCount()) + "</b></div><div class='help'>" + mcRequestLabel + ": <b>" + String(multiControllerGetRequestedGroupPercent(), 1) + "%</b> — toegepast: <b>" + String(multiControllerGetAssignedGroupPercent(), 1) + "%</b></div><div class='help'>Scenario: <b>" + multiControllerGetScenarioLabel() + "</b></div>";

  page += "<div class='row nb'><div class='k'>Multi Controller actief</div><div class='v'>";
  page += String("<span class='switch ") + (mcEnabled ? "on" : "") + "' onclick='sw(this,false);setTimeout(applyMcEnabledUi,0)'>";
  page += String("<input type='checkbox' id='mc_en' name='mc_en' value='1' ") + (mcEnabled ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<div class='help'>Toggle uit = <b>standalone lokaal</b>. Toggle aan = Multi Controller actief op de laatst gekozen rol <b>" + String((uiRole == MC_ROLE_SLAVE) ? "slave" : "master") + "</b>.</div>";

  page += "<div id='mc_enabled_panel'";
  if (!mcEnabled) page += " style='display:none'";
  page += ">";

  page += "<label>Werking zodra Multi Controller aan staat</label>";
  page += "<select id='mc_role' name='mc_role'>";
  page += "<option value='1'" + mcSelInt(uiRole, MC_ROLE_MASTER) + ">Master</option>";
  page += "<option value='2'" + mcSelInt(uiRole, MC_ROLE_SLAVE) + ">Slave</option>";
  page += "</select>";
  page += "<div class='help'>De toggle bepaalt of Multi Controller actief is. Deze rol bepaalt alleen <b>hoe</b> Multi Controller werkt zodra hij aan staat.</div>";

  if (uiRole == MC_ROLE_MASTER) {
    String modeLabel = "lokaal";
    if (mcMode == MC_MODE_NETWORK) modeLabel = "netwerk";
    else if (mcMode == MC_MODE_HYBRID) modeLabel = "mix lokaal + netwerk";
    page += "<div class='help'>Master: controller 1 blijft lokaal op GPIO25. Voor controller 2..3 kies je nu per slot <b>Lokaal</b> of <b>Netwerk</b>. De oude architectuurkeuze is hier niet meer nodig; actuele mix: <b>" + modeLabel + "</b>.</div>";
  } else {
    page += "<div class='help'>Slave: controller 1 blijft lokaal op GPIO25; controller 2 is de vaste master-koppeling; controller 3 wordt niet gebruikt. Deze ESP regelt in slave-modus niet zelf, maar voert alleen opdrachten van de master uit.</div>";
    page += "<input type='hidden' name='mc_mode' value='2'>";
  }

  if (uiRole == MC_ROLE_SLAVE) {
    page += "<div class='row2'>";
    page += "<div><label class='mini'>Aantal controllers in groep</label><input value='2 (vast in slave-modus)' disabled style='opacity:.6;cursor:not-allowed;'><input type='hidden' name='mc_cnt' value='2'></div>";
    page += "<div><label class='mini'>Eigen controller-ID</label><input value='1 (lokale slave-output)' disabled style='opacity:.6;cursor:not-allowed;'><input type='hidden' name='mc_self' value='1'></div>";
    page += "</div>";
    mcAppendYesNoSelect(page, "mc_legovr", mcSlaveAllowLocalLegionellaOverride, "Lokale legionella mag master overrulen");
    page += "<div class='help'>Alleen in slave-modus. Staat dit op Ja, dan mag deze slave tijdelijk zijn lokale legionella-programma draaien alsof hij lokaal standalone werkt, op basis van zijn eigen temperatuursensoren en 7-dagenlogica. Buiten legionella blijft de master leidend.</div>";
  } else {
    page += "<div class='row2'>";
    page += "<div><label class='mini'>Aantal controllers in groep</label><input type='number' min='1' max='3' name='mc_cnt' value='" + String(mcControllerCount) + "'></div>";
    page += "<div><label class='mini'>Eigen controller-ID</label><input type='number' min='1' max='3' name='mc_self' value='" + String(mcLocalControllerId) + "'></div>";
    page += "</div>";
  }

  page += "<label>Groepsnaam</label><input name='mc_group' value='" + mcHtmlEscape(mcGroupName) + "' placeholder='Bijv. woning, beneden, boilergroep'>";
  page += "<div class='help'><b>Regel:</b> netwerk-controllers mogen alleen op de laatste actieve positie(s) staan. Dus <b>lokaal / lokaal / netwerk</b> of <b>lokaal / netwerk / netwerk</b> is goed, maar <b>lokaal / netwerk / lokaal</b> wordt bij opslaan geblokkeerd.</div>";
  page += "<div class='help'>Zonne-overschot = sequentieel. Forceren/prioriteit = per actief slot, binnen safety.</div>";
  page += "<div class='help'>Veiligheid blijft altijd leidend.</div>";

  if (uiRole == MC_ROLE_MASTER) {
    page += "<div class='help'>Master status: netwerk-slots ingesteld/online/writeback OK: <b>" + String(multiControllerGetConfiguredPeerCount()) + " / " + String(multiControllerGetOnlinePeerCount()) + " / " + String(multiControllerGetWritebackHealthyPeerCount()) + "</b></div>";
    page += "<div class='help'>Groepsplanner: <b>" + String(mcGroupPriorityPlannerActive ? "actief" : "wacht") + "</b> — reden: <b>" + mcHtmlEscape(mcGroupPriorityPlannerReason) + "</b></div>";
    page += "<div class='help'>Groepsmodel: <b>" + String(multiControllerGetGroupModelParticipantCount()) + "</b> deelnemers — lokaal <b>" + String(multiControllerGetGroupModelLocalParticipantCount()) + "</b> — netwerk <b>" + String(multiControllerGetGroupModelRemoteParticipantCount()) + "</b></div>";
    for (uint8_t gi = 0; gi < mcGroupModelCount; gi++) {
      const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[gi];
      page += "<div class='help'>Deelnemer " + String(gi + 1) + ": <b>" + mcHtmlEscape(gp.name.length() ? gp.name : String("onbekend")) + "</b> — bron <b>" + (gp.local ? String("lokaal") : (String("netwerk ") + gp.peerIp)) + "</b> — output <b>" + String(gp.peerOutputSlot) + "</b> — fase <b>L" + String(gp.phase) + "</b> — op/af <b>" + String(gp.rampUpOrder) + "/" + String(gp.rampDownOrder) + "</b> — start <b>" + String(gp.startAfterPercent) + "%</b> — min/max <b>" + String(gp.minPercent) + "% / " + String(gp.maxPercent) + "%</b> — gevraagd/toegepast <b>" + String(gp.requestedPct, 1) + "% / " + String(gp.assignedPct, 1) + "%</b>";
      if (gp.blocked) page += " — blok <b>ja</b>";
      if (gp.blockReason.length()) page += " — reden: <b>" + mcHtmlEscape(gp.blockReason) + "</b>";
      page += "</div>";
    }
  } else {
    page += "<div class='help'>Slave status: TCP server <b>" + String(multiControllerTcpSlaveServerListening() ? "luistert" : "uit") + "</b> op poort <b>" + String(multiControllerGetTcpControlPort()) + "</b> — master-link online/ack OK: <b>" + String(multiControllerGetOnlinePeerCount()) + " / " + String(multiControllerGetWritebackHealthyPeerCount()) + "</b></div>";
    page += "<div class='help'>Remote target-control: <b>" + String(multiControllerIsRemoteTargetControlActive() ? "actief" : "veilig teruggevallen") + "</b> — TCP leeftijd: <b>" + String(multiControllerGetPeerTcpAgeMs(1)) + " ms</b> — remote target leeftijd: <b>" + String(multiControllerGetRemoteTargetAgeMs()) + " ms</b>";
    if (multiControllerGetRemoteTargetOwnerIp().length()) page += " — master: <b>" + mcHtmlEscape(multiControllerGetRemoteTargetOwnerIp()) + "</b>";
    page += " — reden: <b>" + mcHtmlEscape(multiControllerGetRemoteTargetReason()) + "</b></div>";
  }

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (uiRole == MC_ROLE_SLAVE && i >= 2) continue;
    const MultiControllerSlotConfig& s = mcSlots[i];
    String prefix = String("mc_s") + String(i + 1) + String("_");
    bool slotConfigured = (i < mcControllerCount);

    page += "<div class='sep'></div>";
    page += "<b>" + String((uiRole == MC_ROLE_SLAVE && i == 1) ? "Master-koppeling" : (String("Controller ") + String(i + 1))) + "</b>";
    page += slotConfigured ? " <span class='tag'>ingesteld</span>" : " <span class='tag'>reserve</span>";

    if (i == 0) {
      page += "<div class='help'>Controller 1 = vaste lokale uitgang op <b>GPIO25</b>.</div>";
    }

    if (i > 0) {
      page += "<label>Type</label>";
      if (uiRole == MC_ROLE_SLAVE && i == 1) {
        page += "<input value='Netwerk-controller (master ESP)' disabled style='opacity:.6;cursor:not-allowed;'>";
        page += "<input type='hidden' name='" + prefix + "type' value='2'>";
      } else {
        page += "<select id='" + prefix + "type' name='" + prefix + "type' data-mc-slot='" + String(i + 1) + "'>";
        page += "<option value='1'" + mcSelInt(s.slotType, MC_SLOT_LOCAL_PWM) + ">Lokale PWM-uitgang</option>";
        page += "<option value='2'" + mcSelInt(s.slotType, MC_SLOT_NETWORK_PEER) + ">Netwerk-controller (slave ESP)</option>";
        page += "</select>";
      }
    }

    if (i > 0 && s.slotType == MC_SLOT_NETWORK_PEER) {
      const MultiControllerPeerRuntime& pr = mcPeerRuntime[i];
      page += "<div class='help'>" + String((uiRole == MC_ROLE_SLAVE && i == 1) ? "Master" : "Slave") + " status: <b>" + mcHtmlEscape(mcPeerStatusLabel(i)) + "</b> — TCP verbonden: <b>" + String(multiControllerIsPeerTcpConnected(i) ? "ja" : "nee") + "</b> — laatste status: <b>" + mcHtmlEscape(mcPeerLastSeenLabel(i)) + "</b></div>";
      if (mcPeerHasUsableCachedStatus(pr, millis())) {
        page += "<div class='help'>Laatste bekende firmware/scenario: <b>" + mcHtmlEscape(pr.fw.length() ? pr.fw : String("onbekend")) + "</b> / <b>" + mcHtmlEscape(pr.scenario.length() ? pr.scenario : String("onbekend")) + "</b> — outputs actief/effectief: <b>" + String(pr.activeOutputs) + "/" + String(pr.effectiveOutputs) + "</b></div>";
      }
      page += "<div class='help'>TCP control: RTT <b>" + String(multiControllerGetPeerTcpRoundTripMs(i)) + " ms</b> — reconnects <b>" + String(multiControllerGetPeerTcpReconnectCount(i)) + "</b> — laatste ack <b>" + String(pr.lastWriteOkMs ? String((millis() - pr.lastWriteOkMs) / 1000UL) + "s geleden" : String("nog niet")) + "</b></div>";
      if (pr.lastWriteError.length()) page += "<div class='help'>Laatste push-fout: <b>" + mcHtmlEscape(pr.lastWriteError) + "</b></div>";
      if (pr.lastError.length()) page += "<div class='help'>Laatste netwerkfout: <b>" + mcHtmlEscape(pr.lastError) + "</b></div>";
    }

    page += "<div class='row2'>";
    page += "<div><label class='mini'>Naam</label><input name='" + prefix + "name' value='" + mcHtmlEscape(s.name) + "' placeholder='Bijv. Boiler boven'></div>";
    if (i == 0) {
      page += "<div><label class='mini'>Peer IP-adres</label><input value='' placeholder='Niet van toepassing voor Controller 1' disabled style='opacity:.6;cursor:not-allowed;'></div>";
    } else {
      bool ipActive = (s.slotType == MC_SLOT_NETWORK_PEER);
      String ipLabel = (uiRole == MC_ROLE_SLAVE && i == 1) ? "Master IP-adres" : "Peer IP-adres";
      String ipPlaceholder = (uiRole == MC_ROLE_SLAVE && i == 1) ? "IP van master ESP" : "IP van slave ESP";
      page += "<div><label class='mini'>" + ipLabel + "</label>";
      page += "<input id='" + prefix + "ip' name='" + prefix + "ip' value='" + mcHtmlEscape(s.peerIp) + "' placeholder='" + ipPlaceholder + "'";
      if (!ipActive) page += " disabled style='display:none'";
      page += ">";
      page += "<input id='" + prefix + "ip_na' value='' placeholder='Niet van toepassing voor lokaal slot' disabled style='" + String(ipActive ? "display:none" : "opacity:.6;cursor:not-allowed;") + "'>";
      page += "</div>";
    }
    page += "</div>";

    page += "<label>Lokale PWM-pin</label>";
    if (i == 0) {
      page += "<select name='" + prefix + "pin'>";
      page += "<option value='25' selected>GPIO25 (vast)</option>";
      page += "</select>";
    } else {
      bool pinActive = (s.slotType != MC_SLOT_NETWORK_PEER);
      page += "<select id='" + prefix + "pin' name='" + prefix + "pin'";
      if (!pinActive) page += " disabled style='display:none'";
      page += ">";
      page += "<option value='26'" + mcSelInt(s.pwmPin, 26) + ">GPIO26</option>";
      page += "<option value='27'" + mcSelInt(s.pwmPin, 27) + ">GPIO27</option>";
      page += "<option value='32'" + mcSelInt(s.pwmPin, 32) + ">GPIO32</option>";
      page += "</select>";
      page += "<input id='" + prefix + "pin_na' value='' placeholder='Niet van toepassing voor netwerk-slot' disabled style='" + String(pinActive ? "display:none" : "opacity:.6;cursor:not-allowed;") + "'>";
    }

    if (multiControllerIsLocalPwmSlotActive(i)) {
      page += "<div class='help'>Runtime: kanaal <b>" + String(multiControllerGetLocalPwmSlotChannel(i)) + "</b> · <b>GPIO" + String(multiControllerGetLocalPwmSlotPin(i)) + "</b> · <b>" + String(multiControllerIsLocalPwmSlotAttached(i) ? "gereed" : "niet gekoppeld") + "</b> · <b>" + String(multiControllerGetLocalPwmSlotPercent(i), 1) + "%</b></div>";
    } else if (slotConfigured && i > 0 && s.slotType == MC_SLOT_LOCAL_PWM) {
      if (multiControllerIsFallbackActive()) page += "<div class='help'>Niet actief door fallback.</div>";
      else page += "<div class='help'>Nog niet actief in runtime.</div>";
    } else if (slotConfigured && i > 0 && s.slotType == MC_SLOT_NETWORK_PEER) {
      page += "<div class='help'>Netwerk-slot: de master plant ook deze controller op zijn ingestelde fase; de slave voert uit en houdt lokaal alleen een harde noodrem actief.</div>";
    }

    page += "<div class='row2'>";
    page += "<div><label class='mini'>Volgende mee vanaf PWM %</label><input type='number' min='0' max='100' name='" + prefix + "start' value='" + String(s.startAfterPercent) + "'></div>";
    page += "</div>";

    page += "<div class='row2'>";
    page += "<div><label class='mini'>Volgorde opregelen</label><input type='number' min='1' max='3' name='" + prefix + "up' value='" + String(s.rampUpOrder) + "'></div>";
    page += "<div><label class='mini'>Volgorde terugregelen</label><input type='number' min='1' max='3' name='" + prefix + "down' value='" + String(s.rampDownOrder) + "'></div>";
    page += "</div>";

    page += "<div class='row2'>";
    page += "<div><label class='mini'>Min PWM %</label><input type='number' min='0' max='100' name='" + prefix + "min' value='" + String(s.minPercent) + "'></div>";
    page += "<div><label class='mini'>Max PWM %</label><input type='number' min='0' max='100' name='" + prefix + "max' value='" + String(s.maxPercent) + "'></div>";
    page += "</div>";

    page += "<div class='row2'><div>";
    mcAppendYesNoSelect(page, prefix + "sun", s.useOnSurplus, "Meedoen bij zonne-overschot");
    page += "</div><div>";
    mcAppendYesNoSelect(page, prefix + "cheap", s.useOnCheap, "Meedoen bij goedkope prijs");
    page += "</div></div>";

    page += "<div class='row2'><div>";
    mcAppendYesNoSelect(page, prefix + "neg", s.useOnNegative, "Meedoen bij negatieve prijs");
    page += "</div><div>";
    mcAppendYesNoSelect(page, prefix + "force", s.allowManualForce, "Forceren / prioriteitswarmte toestaan");
    page += "</div></div>";
  }

  page += "</div>";
  page += "<div class='row' style='margin-top:14px'>";
  page += "<button class='btn' type='submit' name='_section' value='multi' data-section='multi' formaction='/settings/save'>Opslaan</button>";
  page += "</div>";
  page += "</div></details>";
  return page;
}
