static void gc_draw_players_list(
    tenv* env, float u, float width, const char* own_name
) {
    int roster_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(global_chat_net) :
            0;

    if (roster_count <= 0) {
        igTextDisabled(
            (global_chat_net != NULL && jsr_network_is_connected(global_chat_net)) ?
                "No players online." :
                "Not connected to team chat."
        );
        return;
    }

    if (roster_count > 64) {
        roster_count = 64;
    }

    static const ImU32 avatar_cols[6] = {
        IM_COL32(224, 112, 154, 255),
        IM_COL32(56, 182, 165, 255),
        IM_COL32(124, 74, 232, 255),
        IM_COL32(230, 160, 60, 255),
        IM_COL32(77, 140, 255, 255),
        IM_COL32(150, 200, 90, 255)
    };

    gc_loc locs[GC_MAX_LOCS];
    int loc_n = gc_load_locs(locs);

    gc_player_row rows[64];
    int n = 0;

    for (int i = 0; i < roster_count; i++) {
        gc_player_row* r = &rows[n];

        memset(r, 0, sizeof(*r));
        r->ping = -1;

        if (
            !jsr_network_roster_name(
                global_chat_net, i, r->name, sizeof(r->name)
            )
        ) {
            continue;
        }

        jsr_network_roster_owner(
            global_chat_net, i, r->owner, sizeof(r->owner)
        );

        const gc_loc* l = gc_find_loc(locs, loc_n, r->name);

        if (l != NULL) {
            r->has_loc = true;
            r->score = l->score;
            r->ping = l->ping;
            r->emoji = l->emoji;
            r->sos = l->sos;
            snprintf(r->ip, sizeof(r->ip), "%s", l->ip);
        }

        r->you =
            own_name != NULL &&
            own_name[0] != '\0' &&
            strcmp(own_name, r->name) == 0;

        n++;
    }

    qsort(rows, (size_t)n, sizeof(rows[0]), gc_player_cmp);

    float lh = igGetTextLineHeight();
    float row_h = lh * 2.9f;
    float gap = u * 0.4f;

    screen curr_scr = env->usr->gdata.curr_screen;
    const char* own_ip = env->usr->usrs.ipv4;
    float pulse = 0.5f + 0.5f * sinf((float)igGetTime() * 5.0f);

    for (int ri = 0; ri < n; ri++) {
        const gc_player_row* pr = &rows[ri];

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImVec2 pmax = gc_add(p, width, row_h);
        ImDrawList* dl = igGetWindowDrawList();

        bool located =
            gc_locate_name[0] != '\0' &&
            strcmp(gc_locate_name, pr->name) == 0;

        igPushID_Str(pr->name);

        /* PLAY (title screen) / SWITCH (in a match on another server) on
         * this teammate's server. Submitted BEFORE the row button: when
         * items overlap, ImGui gives the tap to the first one submitted. */
        int play_kind = 0;

        if (
            !pr->you && pr->has_loc && pr->ip[0] != '\0' &&
            width > u * 12.0f
        ) {
            if (curr_scr == TITLE_SCREEN) {
                play_kind = 1;
            } else if (curr_scr == PLAYING && strcmp(pr->ip, own_ip) != 0) {
                play_kind = 2;
            }
        }

        bool confirming =
            play_kind == 2 &&
            strcmp(gc_switch_pending, pr->name) == 0 &&
            igGetTime() < gc_switch_until;

        float play_w =
            play_kind != 0 ?
                gc_text_w(play_kind == 2 ? "SWITCH" : "PLAY") + u * 1.4f :
                0.0f;
        float play_h = lh * 1.8f;
        ImVec2 play_mn = gc_v2(
            pmax.x - u * 0.7f - play_w,
            p.y + (row_h - play_h) * 0.5f
        );
        bool play_clicked = false;
        bool play_hot = false;

        if (play_kind != 0) {
            igSetCursorScreenPos(play_mn);

            play_clicked = igInvisibleButton(
                "##gc_play", gc_v2(play_w, play_h), ImGuiButtonFlags_None
            );

            play_hot = igIsItemHovered(0) || igIsItemActive();

            igSetCursorScreenPos(p);
        }

        bool row_clicked = igInvisibleButton(
            "##gc_prow", gc_v2(width, row_h), ImGuiButtonFlags_None
        );

        bool hovered = igIsItemHovered(0) || igIsItemActive();

        ImU32 bg =
            located ? IM_COL32(124, 74, 232, 95) :
            pr->sos ? IM_COL32(224, 56, 44, 50) :
            hovered ? IM_COL32(255, 255, 255, 24) :
                      IM_COL32(255, 255, 255, 12);

        ImDrawList_AddRectFilled(dl, p, pmax, bg, u * 0.7f, 0);

        if (located) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(190, 160, 255, 170),
                u * 0.7f, 0, 1.2f
            );
        } else if (pr->sos) {
            ImDrawList_AddRect(
                dl, p, pmax,
                IM_COL32(255, 90, 74, (int)(110.0f + pulse * 90.0f)),
                u * 0.7f, 0, 1.2f
            );
        } else if (pr->you) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(125, 255, 176, 80),
                u * 0.7f, 0, 1.0f
            );
        }

        /* Avatar with an online dot. */
        unsigned hash = 0;

        for (const char* c = pr->name; *c; c++) {
            hash = hash * 31u + (unsigned char)*c;
        }

        float cy = p.y + row_h * 0.5f;
        float r_av = lh * 0.95f;
        ImVec2 ac = gc_v2(p.x + u * 0.9f + r_av, cy);

        ImDrawList_AddCircleFilled(
            dl, ac, r_av, avatar_cols[hash % 6u], 24
        );

        char initial[8];
        gc_initial(pr->name, initial);

        gc_text_center(
            dl,
            gc_v2(ac.x - r_av, ac.y - r_av),
            gc_v2(r_av * 2.0f, r_av * 2.0f),
            IM_COL32(255, 255, 255, 255),
            initial
        );

        ImVec2 dot = gc_v2(ac.x + r_av * 0.72f, ac.y + r_av * 0.72f);
        ImDrawList_AddCircleFilled(
            dl, dot, u * 0.38f, IM_COL32(28, 16, 52, 255), 12
        );
        ImDrawList_AddCircleFilled(
            dl, dot, u * 0.26f, IM_COL32(125, 255, 176, 255), 12
        );

        float tx = ac.x + r_av + u * 0.8f;
        float right =
            pmax.x - u * 0.7f -
            (play_kind != 0 ? play_w + u * 0.6f : 0.0f);
        float ty1 = cy - lh * 1.1f;
        float ty2 = cy + lh * 0.1f;

        /* Tags on the right of line 1. */
        float tag_h = lh * 1.2f;
        float tag_y = ty1 - (tag_h - lh) * 0.5f;
        float tag_x = right;

        if (pr->you) {
            gc_tag(dl, &tag_x, tag_y, tag_h, "YOU", IM_COL32(124, 74, 232, 230), u);
        }

        if (pr->sos) {
            gc_tag(dl, &tag_x, tag_y, tag_h, "SOS", IM_COL32(224, 56, 44, 255), u);
        }

        /* Line 1: owner (red) + name + emoji. */
        float avail1 = tag_x - tx;
        float x = tx;

        if (pr->owner[0] != '\0') {
            char ow[40];
            gc_clip_text(ow, sizeof(ow), pr->owner, avail1 * 0.45f);

            ImDrawList_AddText_Vec2(
                dl, gc_v2(x, ty1), IM_COL32(242, 77, 77, 255), ow, NULL
            );

            x += gc_text_w(ow) + u * 0.4f;
        }

        float ew0 = gc_pemoji_w(pr->emoji, lh);
        float ew = ew0 > 0.0f ? ew0 + u * 0.4f : 0.0f;

        char nm[48];
        gc_clip_text(nm, sizeof(nm), pr->name, avail1 - (x - tx) - ew);

        ImDrawList_AddText_Vec2(
            dl, gc_v2(x, ty1),
            pr->you ?
                IM_COL32(125, 255, 176, 255) :
                IM_COL32(180, 156, 245, 255),
            nm, NULL
        );

        x += gc_text_w(nm) + u * 0.4f;

        if (ew0 > 0.0f) {
            gc_pemoji_draw(dl, gc_v2(x, ty1), pr->emoji, lh);
        }

        /* Line 2: score / server on the left, ping on the right. */
        ImU32 dim = IM_COL32(140, 133, 170, 230);
        float ping_w = 0.0f;
        char ping_s[16] = "";

        if (pr->has_loc && pr->ping >= 0) {
            snprintf(ping_s, sizeof(ping_s), "%d ms", pr->ping);
            ping_w = gc_text_w(ping_s) + u * 0.8f;

            ImDrawList_AddCircleFilled(
                dl, gc_v2(right - ping_w + u * 0.25f, ty2 + lh * 0.5f),
                u * 0.22f, gc_ping_color(pr->ping), 10
            );

            ImDrawList_AddText_Vec2(
                dl, gc_v2(right - ping_w + u * 0.8f, ty2),
                gc_ping_color(pr->ping), ping_s, NULL
            );
        }

        char info[128];

        if (pr->has_loc) {
            char sc[24];
            gc_fmt_num(sc, sizeof(sc), pr->score);

            if (pr->ip[0] != '\0') {
                snprintf(info, sizeof(info), "%s  -  %s", sc, pr->ip);
            } else {
                snprintf(info, sizeof(info), "%s", sc);
            }
        } else if (pr->you) {
            if (curr_scr == PLAYING) {
                snprintf(info, sizeof(info), "%s", own_ip);
            } else {
                snprintf(info, sizeof(info), "In the menu");
            }
        } else {
            snprintf(info, sizeof(info), "No game info");
        }

        char info_c[128];
        gc_clip_text(
            info_c, sizeof(info_c), info,
            right - ping_w - u * 0.5f - tx
        );

        ImDrawList_AddText_Vec2(dl, gc_v2(tx, ty2), dim, info_c, NULL);

        /* PLAY / SWITCH button visual + action. */
        if (play_kind != 0) {
            ImU32 pc =
                play_kind == 1 ?
                    (play_hot ? IM_COL32(150, 100, 250, 255) : IM_COL32(124, 74, 232, 255)) :
                confirming ?
                    IM_COL32(230, 150, 40, 255) :
                    (play_hot ? IM_COL32(95, 80, 160, 255) : IM_COL32(70, 56, 120, 255));

            ImDrawList_AddRectFilled(
                dl, play_mn, gc_add(play_mn, play_w, play_h),
                pc, play_h * 0.5f, 0
            );

            gc_text_center(
                dl, play_mn, gc_v2(play_w, play_h),
                IM_COL32(255, 255, 255, 255),
                play_kind == 1 ? "PLAY" : (confirming ? "SURE?" : "SWITCH")
            );

            if (play_clicked && !gc_scroll_dragged) {
                if (play_kind == 1) {
                    gc_play_on_server(env, pr->ip);
                } else if (confirming) {
                    gc_switch_pending[0] = '\0';
                    gc_play_on_server(env, pr->ip);
                } else {
                    snprintf(
                        gc_switch_pending, sizeof(gc_switch_pending),
                        "%s", pr->name
                    );
                    gc_switch_until = igGetTime() + 3.0;
                }
            }
        }

        /* Tap (not drag) toggles the highlight. */
        if (row_clicked && !gc_scroll_dragged) {
            if (located) {
                gc_locate_name[0] = '\0';
            } else {
                snprintf(gc_locate_name, sizeof(gc_locate_name), "%s", pr->name);
            }
        }

        igPopID();

        igDummy(gc_v2(width, gap));
    }
}

/* Slim draggable scrollbar for the message list, drawn flush with the
 * right edge of the main area. Same absolute-drag logic as before: a
 * fat invisible hit area (fingers) and no per-frame delta
 * accumulation (touch-move events get coalesced). */
static void gc_message_scrollbar(
    ImVec2 track_pos,
    float track_h,
    float hit_w,
    float view_h,
    float scroll_max,
    float scroll_y,
    ImGuiWindow* msg_window,
    float u
) {
    static float drag_anchor_mouse_y = 0.0f;
    static float drag_anchor_scroll = 0.0f;

    float bar_w = fmaxf(5.0f, u * 0.3f);
    float bar_x = track_pos.x + (hit_w - bar_w) * 0.5f;

    float thumb_h = track_h;

    if (scroll_max > 0.0f) {
        thumb_h = track_h * (view_h / (view_h + scroll_max));
        thumb_h = gc_clampf(thumb_h, u * 2.0f, track_h);
    }

    float ratio = scroll_max > 0.0f ? (scroll_y / scroll_max) : 0.0f;
    float thumb_y = track_pos.y + ratio * (track_h - thumb_h);

    ImDrawList* dl = igGetWindowDrawList();

    ImDrawList_AddRectFilled(
        dl,
        gc_v2(bar_x, track_pos.y),
        gc_v2(bar_x + bar_w, track_pos.y + track_h),
        IM_COL32(176, 148, 245, 28),
        bar_w * 0.5f, 0
    );

    ImDrawList_AddRectFilled(
        dl,
        gc_v2(bar_x, thumb_y),
        gc_v2(bar_x + bar_w, thumb_y + thumb_h),
        IM_COL32(191, 165, 245, 170),
        bar_w * 0.5f, 0
    );

    igSetCursorScreenPos(track_pos);
    igInvisibleButton(
        "##gc_scrollbar",
        gc_v2(hit_w, track_h),
        ImGuiButtonFlags_None
    );

    if (scroll_max <= 0.0f) {
        return;
    }

    float usable = track_h - thumb_h;

    if (usable <= 0.0f) {
        return;
    }

    if (igIsItemActivated()) {
        ImVec2 mouse;
        igGetMousePos(&mouse);

        float click_ratio =
            gc_clampf(
                (mouse.y - track_pos.y - thumb_h * 0.5f) / usable,
                0.0f, 1.0f
            );

        float jump = click_ratio * scroll_max;

        igSetScrollY_WindowPtr(msg_window, jump);

        drag_anchor_mouse_y = mouse.y;
        drag_anchor_scroll = jump;
    }

    if (igIsItemActive()) {
        ImVec2 mouse;
        igGetMousePos(&mouse);

        float new_scroll =
            drag_anchor_scroll +
            ((mouse.y - drag_anchor_mouse_y) / usable) * scroll_max;

        igSetScrollY_WindowPtr(
            msg_window,
            gc_clampf(new_scroll, 0.0f, scroll_max)
        );
    }
}

/* Ends a scrolling child that was begun with ImGuiWindowFlags_NoScrollbar:
 * finger/mouse drag scrolls it, and the slim custom scrollbar is drawn
 * next to it (only when there is something to scroll). */
static void gc_end_scroll_child(
    ImVec2 area_pos,
    float area_h,
    float hit_w,
    float child_w,
    float u,
    bool allow_drag_scroll,
    bool over_items
) {
    if (
        allow_drag_scroll &&
        igIsWindowHovered(0) &&
        (over_items || !igIsAnyItemActive()) &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollY_Float(
            igGetScrollY() - igGetIO_Nil()->MouseDelta.y
        );
        gc_scroll_dragged = true;
    }

    float scroll_max = igGetScrollMaxY();
    float scroll_y = igGetScrollY();
    ImGuiWindow* win = igGetCurrentWindow();

    igEndChild();

    if (scroll_max > 1.0f) {
        gc_message_scrollbar(
            gc_v2(area_pos.x + child_w, area_pos.y),
            area_h,
            hit_w,
            area_h,
            scroll_max,
            scroll_y,
            win,
            u
        );
    }
}

/* Chat messages: one rounded bubble per message (name line + wrapped
 * text), consecutive messages from the same sender in the same minute
 * are grouped, join/leave notices are compact one-liners. */
static void gc_draw_messages(
    tenv* env,
    ImVec2 area_pos,
    ImVec2 area_size,
    float u,
    bool allow_drag_scroll
) {
    const char* own_name = env->usr->usrs.nickname;

    float hit_w = u * 1.6f;
    float child_w = area_size.x - hit_w;

    if (child_w < 60.0f) child_w = 60.0f;

    igSetCursorScreenPos(area_pos);

    igBeginChild_Str(
        "##global_chat_messages",
        gc_v2(child_w, area_size.y),
        0,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground
    );

    static int last_seen_message_count = 0;

    ImVec2 avail;
    igGetContentRegionAvail(&avail);

    bool was_at_bottom =
        igGetScrollY() >= igGetScrollMaxY() - 1.0f;

    ImDrawList* dl = igGetWindowDrawList();

    float w = avail.x;
    float lh = igGetTextLineHeight();
    float pad_x = u * 0.75f;
    float pad_y = u * 0.42f;
    float wrap_w = w - pad_x * 2.0f;

    if (wrap_w < 20.0f) wrap_w = 20.0f;

    gc_loc locs[GC_MAX_LOCS];
    int loc_n = gc_load_locs(locs);

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(global_chat_net);

    ImU32 dim = IM_COL32(150, 142, 185, 210);
    ImU32 dimmer = IM_COL32(120, 112, 155, 200);

    if (global_chat_message_count == 0) {
        ImVec2 cp;
        igGetCursorScreenPos(&cp);

        float box_h = area_size.y * 0.5f;

        gc_text_center(
            dl, cp, gc_v2(w, box_h), dim,
            is_connected ? "No messages yet" : "Connecting to team chat..."
        );
        gc_text_center(
            dl, gc_add(cp, 0.0f, lh * 1.5f), gc_v2(w, box_h), dimmer,
            "Say hi to your team"
        );

        igDummy(gc_v2(w, box_h));
    }

    char prev_hm[8] = "";
    const char* prev_name = NULL;
    bool prev_normal = false;
    bool divider_drawn = false;
    bool divider_scrolled = false;

    for (int i = 0; i < global_chat_message_count; i++) {
        global_chat_message* m = &global_chat_messages[i];

        bool is_system = strcmp(m->name, "[SYSTEM]") == 0;
        bool is_own =
            own_name != NULL &&
            own_name[0] != '\0' &&
            strcmp(m->name, own_name) == 0;

        if (
            gc_divider_active &&
            !divider_drawn &&
            m->seq > gc_divider_after
        ) {
            divider_drawn = true;

            ImVec2 dp;
            igGetCursorScreenPos(&dp);

            const char* dtxt = "NEW MESSAGES";
            float dtw = gc_text_w(dtxt);
            float dh = lh + u * 0.7f;
            float dmid = dp.y + dh * 0.5f;
            float gap = u * 0.7f;
            float cx = dp.x + w * 0.5f;
            ImU32 dcol = IM_COL32(242, 120, 120, 255);
            ImU32 lcol = IM_COL32(242, 120, 120, 110);

            ImDrawList_AddLine(
                dl, gc_v2(dp.x + pad_x * 0.5f, dmid),
                gc_v2(cx - dtw * 0.5f - gap, dmid), lcol, 1.0f
            );
            ImDrawList_AddLine(
                dl, gc_v2(cx + dtw * 0.5f + gap, dmid),
                gc_v2(dp.x + w - pad_x * 0.5f, dmid), lcol, 1.0f
            );
            ImDrawList_AddText_Vec2(
                dl, gc_v2(cx - dtw * 0.5f, dp.y + (dh - lh) * 0.5f),
                dcol, dtxt, NULL
            );

            igDummy(gc_v2(w, dh));

            if (gc_divider_scroll) {
                /* Jump so the divider sits near the top of the view. */
                igSetScrollHereY(0.08f);
                gc_divider_scroll = false;
                divider_scrolled = true;
            }

            prev_normal = false;
        }

        char hm[8];
        snprintf(hm, sizeof(hm), "%.5s", m->time_str);

        igPushID_Int(i);

        bool joined = strcmp(m->text, "joined public chat") == 0;
        bool left = strcmp(m->text, "left public chat") == 0;

        if (!is_system && (joined || left)) {
            const char* verb = joined ? "joined" : "left";
            ImU32 dotc =
                joined ?
                    IM_COL32(125, 255, 176, 255) :
                    IM_COL32(242, 77, 77, 255);

            /* "left" immediately followed by "joined" = a reconnect blip. */
            if (left && i + 1 < global_chat_message_count) {
                global_chat_message* nx = &global_chat_messages[i + 1];

                if (
                    strcmp(nx->text, "joined public chat") == 0 &&
                    strcmp(nx->name, m->name) == 0
                ) {
                    verb = "reconnected";
                    dotc = IM_COL32(255, 204, 77, 255);
                    snprintf(hm, sizeof(hm), "%.5s", nx->time_str);
                    i++;
                }
            }

            prev_normal = false;

            if (gc_show_events) {
                ImVec2 p;
                igGetCursorScreenPos(&p);

                float tw = gc_text_w(hm);
                float tx = p.x + pad_x * 1.4f;

                ImDrawList_AddCircleFilled(
                    dl, gc_v2(p.x + pad_x * 0.7f, p.y + lh * 0.5f),
                    u * 0.22f, dotc, 10
                );

                float max_w =
                    w - pad_x * 1.4f - tw - pad_x -
                    gc_text_w(verb) - u * 0.5f;

                char nm[48];
                gc_clip_text(nm, sizeof(nm), m->name, max_w);

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(tx, p.y), dim, nm, NULL
                );

                tx += gc_text_w(nm) + u * 0.4f;

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(tx, p.y), dimmer, verb, NULL
                );

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(p.x + w - tw - pad_x * 0.5f, p.y),
                    dimmer, hm, NULL
                );

                igDummy(gc_v2(w, lh + u * 0.35f));
            }

            igPopID();
            continue;
        }

        bool grouped =
            !is_system &&
            prev_normal &&
            prev_name != NULL &&
            strcmp(prev_name, m->name) == 0 &&
            strcmp(prev_hm, hm) == 0;

        bool ext = gc_has_non_ascii(m->text);
        bool rich = gc_text_has_sprite(m->text);

        ImVec2 tsz;

        if (rich) {
            tsz.x = wrap_w;
            tsz.y = gc_rich_text(
                m->text, wrap_w, lh, false, dl, gc_v2(0.0f, 0.0f), 0
            );
        } else {
            if (ext) gc_emoji_push();
            igCalcTextSize(&tsz, m->text, NULL, false, wrap_w);
            if (ext) gc_emoji_pop();
        }

        float head_h = grouped ? 0.0f : lh + u * 0.15f;
        float bh = pad_y * 2.0f + head_h + tsz.y;

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImVec2 pmax = gc_add(p, w, bh);

        ImU32 bg =
            is_system ? IM_COL32(62, 186, 255, 26) :
            is_own ? IM_COL32(124, 74, 232, 78) :
                     IM_COL32(255, 255, 255, 13);

        ImDrawList_AddRectFilled(dl, p, pmax, bg, u * 0.65f, 0);

        if (is_system) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(120, 210, 255, 70),
                u * 0.65f, 0, 1.0f
            );
        } else if (is_own) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(190, 160, 255, 60),
                u * 0.65f, 0, 1.0f
            );
        }

        if (!grouped) {
            float hx = p.x + pad_x;
            float hy = p.y + pad_y;
            float tw = gc_text_w(hm);

            ImDrawList_AddText_Vec2(
                dl, gc_v2(pmax.x - pad_x - tw, hy), dimmer, hm, NULL
            );

            float room = (pmax.x - pad_x - tw - u * 0.6f) - hx;

            if (is_system) {
                ImDrawList_AddText_Vec2(
                    dl, gc_v2(hx, hy),
                    IM_COL32(158, 232, 255, 255), "System", NULL
                );
            } else {
                const gc_loc* l = gc_find_loc(locs, loc_n, m->name);
                int eid = l != NULL ? l->emoji : 0;
                float ew0 = gc_pemoji_w(eid, lh);
                float ew = ew0 > 0.0f ? ew0 + u * 0.4f : 0.0f;
                float x = hx;

                if (m->owner[0] != '\0') {
                    char ow[40];
                    gc_clip_text(ow, sizeof(ow), m->owner, room * 0.45f);

                    ImDrawList_AddText_Vec2(
                        dl, gc_v2(x, hy),
                        IM_COL32(242, 77, 77, 255), ow, NULL
                    );

                    x += gc_text_w(ow) + u * 0.4f;
                }

                char nm[48];
                gc_clip_text(nm, sizeof(nm), m->name, room - (x - hx) - ew);

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(x, hy),
                    is_own ?
                        IM_COL32(125, 255, 176, 255) :
                        IM_COL32(181, 156, 245, 255),
                    nm, NULL
                );

                x += gc_text_w(nm) + u * 0.4f;

                if (ew0 > 0.0f) {
                    gc_pemoji_draw(dl, gc_v2(x, hy), eid, lh);
                }
            }
        }

        if (rich) {
            gc_rich_text(
                m->text, wrap_w, lh, true, dl,
                gc_v2(p.x + pad_x, p.y + pad_y + head_h),
                is_system ?
                    IM_COL32(158, 232, 255, 255) :
                    IM_COL32(237, 232, 255, 255)
            );
        } else {
            igSetCursorScreenPos(gc_v2(p.x + pad_x, p.y + pad_y + head_h));
            igPushTextWrapPos(igGetCursorPosX() + wrap_w);
            igPushStyleColor_Vec4(
                ImGuiCol_Text,
                is_system ?
                    (ImVec4){0.62f, 0.91f, 1.0f, 1.0f} :
                    (ImVec4){0.93f, 0.91f, 1.0f, 1.0f}
            );
            if (ext) gc_emoji_push();
            igTextUnformatted(m->text, NULL);
            if (ext) gc_emoji_pop();
            igPopStyleColor(1);
            igPopTextWrapPos();
        }

        igSetCursorScreenPos(p);
        igDummy(gc_v2(w, bh + (grouped ? u * 0.12f : u * 0.3f)));

        prev_normal = !is_system;
        prev_name = m->name;
        snprintf(prev_hm, sizeof(prev_hm), "%s", hm);

        igPopID();
    }

    /* Teammates asking for help show up as cards at the end. */
    gc_draw_sos_cards(u, avail.x, own_name);

    if (
        !divider_scrolled &&
        global_chat_message_count != last_seen_message_count &&
        (was_at_bottom || last_seen_message_count == 0)
    ) {
        /* Only auto-scroll if the player was already at (or near)
         * the bottom. */
        igSetScrollHereY(1.0f);
    }

    last_seen_message_count = global_chat_message_count;

    /* "Jump to latest" button while scrolled up. */
    if (igGetScrollMaxY() - igGetScrollY() > lh * 3.0f) {
        ImVec2 wpos;
        ImVec2 wsz;
        igGetWindowPos(&wpos);
        igGetWindowSize(&wsz);

        float r = u * 1.1f;
        ImVec2 c = gc_v2(
            wpos.x + wsz.x - r - u * 0.4f,
            wpos.y + wsz.y - r - u * 0.4f
        );

        igSetCursorScreenPos(gc_v2(c.x - r, c.y - r));

        bool jump = igInvisibleButton(
            "##gc_jump", gc_v2(r * 2.0f, r * 2.0f), ImGuiButtonFlags_None
        );

        bool hot = igIsItemHovered(0) || igIsItemActive();

        ImDrawList_AddCircleFilled(
            dl, c, r,
            hot ? IM_COL32(150, 100, 250, 245) : IM_COL32(124, 74, 232, 230),
            24
        );

        ImDrawList_AddTriangleFilled(
            dl,
            gc_v2(c.x - r * 0.42f, c.y - r * 0.18f),
            gc_v2(c.x + r * 0.42f, c.y - r * 0.18f),
            gc_v2(c.x, c.y + r * 0.38f),
            IM_COL32(255, 255, 255, 255)
        );

        if (jump) {
            igSetScrollY_Float(igGetScrollMaxY());
        }
    }

    gc_end_scroll_child(
        area_pos, area_size.y, hit_w, child_w, u, allow_drag_scroll, false
    );
}

/* ---- colour emoji picker -------------------------------------------- */

#define GC_TRAY_MAX 12

/* Emoji picked in the chat picker; they are sent after the typed text. */
static int gc_tray[GC_TRAY_MAX];
static int gc_tray_n = 0;

/* Put the picked emoji (as real Unicode) at the end of the message. */
static void gc_tray_flush_into_input(void) {
    for (int i = 0; i < gc_tray_n; i++) {
        unsigned int cp = ntl_emoji_cp_for_index(gc_tray[i]);

        if (cp == 0) {
            continue;
        }

        char enc[8];
        int el = gc_utf8_encode(cp, enc);
        size_t len = strlen(global_chat_input);

        if (len + (size_t)el < sizeof(global_chat_input)) {
            memcpy(global_chat_input + len, enc, (size_t)el + 1);
        }
    }

    gc_tray_n = 0;
}

static float gc_tray_height(float u) {
    return u * 2.0f + u * 0.8f;
}

/* Strip above the input showing the emoji picked so far. Tap one to
 * remove it. */
static void gc_draw_tray(ImVec2 pos, float width, float u) {
    ImDrawList* dl = igGetWindowDrawList();

    float h = gc_tray_height(u);
    float chip_h = u * 2.0f;
    float chip_w = chip_h * 1.15f;
    float pad = u * 0.4f;
    float gx = u * 0.4f;

    ImDrawList_AddRectFilled(
        dl, pos, gc_add(pos, width, h),
        IM_COL32(255, 255, 255, 12), u * 0.8f, 0
    );
    ImDrawList_AddRect(
        dl, pos, gc_add(pos, width, h),
        IM_COL32(190, 160, 255, 55), u * 0.8f, 0, 1.0f
    );

    int remove = -1;

    for (int i = 0; i < gc_tray_n; i++) {
        ImVec2 p = gc_v2(
            pos.x + pad + (float)i * (chip_w + gx), pos.y + pad
        );

        if (p.x + chip_w > pos.x + width - pad) {
            break;
        }

        igSetCursorScreenPos(p);

        char id[16];
        snprintf(id, sizeof(id), "##gctray%d", i);

        bool clicked = igInvisibleButton(
            id, gc_v2(chip_w, chip_h), ImGuiButtonFlags_None
        );

        bool hot = igIsItemHovered(0) || igIsItemActive();

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, chip_w, chip_h),
            hot ? IM_COL32(255, 255, 255, 46) : IM_COL32(255, 255, 255, 22),
            chip_h * 0.5f, 0
        );

        float es = chip_h * 0.78f;
        ImVec2 a = gc_v2(
            p.x + (chip_w - es) * 0.5f, p.y + (chip_h - es) * 0.5f
        );

        ntl_emoji_draw(
            dl, a, gc_v2(a.x + es, a.y + es), gc_tray[i],
            IM_COL32(255, 255, 255, 255)
        );

        if (clicked) {
            remove = i;
        }
    }

    if (remove >= 0) {
        for (int i = remove; i + 1 < gc_tray_n; i++) {
            gc_tray[i] = gc_tray[i + 1];
        }

        gc_tray_n--;
    }
}

/* Size of an emoji grid for `count` chips in `width`: the same pill chips
 * as before, wrapped into rows. If it is taller than max_h it scrolls. */
static void gc_emoji_grid_layout(
    float width, float max_h, float u, int count,
    float* out_chip_h, int* out_cols, float* out_h
) {
    float pad = u * 0.55f;
    float gx = u * 0.45f;
    float chip_h = u * 2.1f;
    float chip_w = chip_h * 1.15f;

    int cols = (int)((width - pad * 2.0f + gx) / (chip_w + gx));

    if (cols < 1) cols = 1;

    int rows = (count + cols - 1) / cols;
    float full = pad * 2.0f + (float)rows * chip_h + (float)(rows - 1) * gx;

    *out_chip_h = chip_h;
    *out_cols = cols;
    *out_h = full > max_h ? max_h : full;
}

/* The colour emoji of the minimap dots, as a scrolling grid. With
 * usrs == NULL (chat picker) a tap adds the emoji to the message; with
 * usrs set (Settings) a tap chooses the profile emoji, plus a "none" chip. */
static void gc_draw_emoji_grid(
    const char* child_id, ImVec2 pos, ImVec2 size, float u,
    float chip_h, int cols, user_settings* usrs
) {
    ImDrawList* pdl = igGetWindowDrawList();

    ImDrawList_AddRectFilled(
        pdl, pos, gc_add(pos, size.x, size.y),
        IM_COL32(255, 255, 255, 12), u * 0.8f, 0
    );
    ImDrawList_AddRect(
        pdl, pos, gc_add(pos, size.x, size.y),
        IM_COL32(190, 160, 255, 55), u * 0.8f, 0, 1.0f
    );

    igSetCursorScreenPos(pos);

    igBeginChild_Str(
        child_id,
        size,
        0,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground
    );

    if (!ntl_emoji_ready()) {
        igTextDisabled("Emoji pictures are not available.");
        igEndChild();
        return;
    }

    if (
        igIsWindowHovered(0) &&
        !igIsAnyItemActive() &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollY_Float(igGetScrollY() - igGetIO_Nil()->MouseDelta.y);
        gc_scroll_dragged = true;
    }

    int total = NTL_EMOJI_COUNT + (usrs != NULL ? 1 : 0);
    float pad = u * 0.55f;
    float gx = u * 0.45f;
    float chip_w = chip_h * 1.15f;
    int rows = (total + cols - 1) / cols;

    ImVec2 base;
    igGetCursorScreenPos(&base);

    ImDrawList* dl = igGetWindowDrawList();

    for (int k = 0; k < total; k++) {
        int idx = usrs != NULL ? k - 1 : k; /* -1 = "none" chip */
        int r = k / cols;
        int c = k % cols;

        ImVec2 p = gc_v2(
            base.x + pad + (float)c * (chip_w + gx),
            base.y + pad + (float)r * (chip_h + gx)
        );

        igSetCursorScreenPos(p);

        char id[16];
        snprintf(id, sizeof(id), "##gcem%d", k);

        bool clicked = igInvisibleButton(
            id, gc_v2(chip_w, chip_h), ImGuiButtonFlags_None
        );

        bool hot = igIsItemHovered(0) || igIsItemActive();

        bool selected =
            usrs != NULL &&
            usrs->profile_emoji_id ==
                (idx < 0 ? 0 : PROFILE_EMOJI_SPRITE_BASE + idx);

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, chip_w, chip_h),
            selected ? IM_COL32(77, 140, 255, 255) :
            hot ? IM_COL32(255, 255, 255, 46) :
                  IM_COL32(255, 255, 255, 20),
            chip_h * 0.5f, 0
        );

        if (idx < 0) {
            gc_text_center(
                dl, p, gc_v2(chip_w, chip_h),
                IM_COL32(255, 255, 255, 255), "-"
            );
        } else {
            float es = chip_h * 0.78f;
            ImVec2 a = gc_v2(
                p.x + (chip_w - es) * 0.5f, p.y + (chip_h - es) * 0.5f
            );

            ntl_emoji_draw(
                dl, a, gc_v2(a.x + es, a.y + es), idx,
                IM_COL32(255, 255, 255, 255)
            );
        }

        if (clicked && !gc_scroll_dragged) {
            if (usrs != NULL) {
                usrs->profile_emoji_id =
                    idx < 0 ? 0 : PROFILE_EMOJI_SPRITE_BASE + idx;
                save_user_settings(usrs);
            } else if (
                gc_tray_n < GC_TRAY_MAX &&
                strlen(global_chat_input) + 4u * (size_t)(gc_tray_n + 1) <
                    sizeof(global_chat_input)
            ) {
                gc_tray[gc_tray_n++] = idx;
            }
        }
    }

    igSetCursorScreenPos(base);
    igDummy(
        gc_v2(
            size.x,
            pad * 2.0f + (float)rows * chip_h + (float)(rows - 1) * gx
        )
    );

    igEndChild();
}

/* "This server requires an access key" screen. Same logic as before,
 * drawn inside the main area. */
static void gc_draw_key_screen(
    tenv* env,
    user_settings* usrs,
    bool rejected,
    ImVec2 pos,
    ImVec2 size,
    float u
) {
    igSetCursorScreenPos(gc_add(pos, u * 0.9f, u * 0.6f));

    igBeginChild_Str(
        "##gc_keyscreen",
        gc_v2(size.x - u * 1.8f, size.y - u * 0.6f),
        0,
        ImGuiWindowFlags_NoBackground
    );

    igText("This server requires an access key.");

    igTextWrapped(
        "Ask your clan owner for your key, "
        "then enter it below."
    );

    igSpacing();

    static char key_input[96] = "";
    static bool key_input_seeded = false;

    if (!key_input_seeded) {
        strncpy(key_input, usrs->public_chat_key, sizeof(key_input) - 1);
        key_input[sizeof(key_input) - 1] = '\0';
        key_input_seeded = true;
    }

    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.5f);
    igPushStyleColor_Vec4(
        ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
    );

    igPushItemWidth(-1.0f);

    igInputTextWithHint(
        "##public_chat_key_input",
        "Access key",
        key_input,
        sizeof(key_input),
        ImGuiInputTextFlags_None,
        NULL,
        NULL
    );

    igPopItemWidth();
    igPopStyleColor(1);
    igPopStyleVar(1);

    if (rejected) {
        igTextColored(
            (ImVec4){0.9f, 0.3f, 0.3f, 1.0f},
            "%s",
            jsr_network_get_last_error(global_chat_net)
        );
    }

    igSpacing();

    ImVec2 p;
    igGetCursorScreenPos(&p);

    float bh = u * 2.4f;
    float bw;
    {
        ImVec2 av;
        igGetContentRegionAvail(&av);
        bw = av.x;
    }

    if (
        gc_button(
            "##gc_connect",
            p,
            gc_v2(bw, bh),
            IM_COL32(124, 74, 232, 255),
            IM_COL32(150, 100, 250, 255),
            u * 0.6f,
            0
        )
    ) {
        if (key_input[0] != '\0') {
            strncpy(
                usrs->public_chat_key,
                key_input,
                sizeof(usrs->public_chat_key) - 1
            );

            usrs->public_chat_key[
                sizeof(usrs->public_chat_key) - 1
            ] = '\0';

            save_user_settings(usrs);

            global_chat_try_connect(env);
        }
    }

    gc_text_center(
        igGetWindowDrawList(), p, gc_v2(bw, bh),
        IM_COL32(255, 255, 255, 255), "Connect"
    );

    igEndChild();
}

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 wp,
    ImVec2 ws
) {
    if (env == NULL || env->usr == NULL) {
        return;
    }

    user_settings* usrs = &env->usr->usrs;

    /* Same panel text scale as before. (This ImGui version has no
     * SetWindowFontScale -- font size is set via PushFont/PopFont, so
     * every return path below must PopFont() to match.) */
    igPushFont(NULL, igGetStyle()->FontSizeBase * 0.85f);

    /* The panel font (JetBrains Mono) has no profile-emoji glyphs;
     * Liberation Sans does, so emoji are drawn with that one. */
    gc_emoji_font = env->usr->imgui_data.regular_font[usrs->ui_font_size];

    if (igIsMouseClicked_Bool(0, false)) {
        gc_scroll_dragged = false;
    }

    ImDrawList* dl = igGetWindowDrawList();

    float u = igGetTextLineHeight();

    const char* own_name = usrs->nickname;

    bool rejected =
        global_chat_net != NULL &&
        jsr_network_is_auth_rejected(global_chat_net);

    bool needs_key =
        usrs->public_chat_key[0] == '\0' || rejected;

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(global_chat_net);

    int online_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(global_chat_net) :
            0;

    int others_sos = gc_others_sos_count(own_name);

    float pulse = 0.5f + 0.5f * sinf((float)igGetTime() * 5.0f);

    bool adjusting_pos =
        global_chat_adjust == GLOBAL_CHAT_ADJUST_POSITION;
    bool adjusting_size =
        global_chat_adjust == GLOBAL_CHAT_ADJUST_SIZE;
    bool adjusting = adjusting_pos || adjusting_size;

    /* ---------- panel + rail backgrounds ---------- */
    float radius = u * 1.1f;
    float rail_w = gc_clampf(ws.x * 0.126f, u * 2.7f, u * 4.2f);
    float alpha = gc_clampf(gc_panel_alpha, 0.05f, 1.0f);

    ImVec2 pmax = gc_add(wp, ws.x, ws.y);

    ImDrawList_AddRectFilled(
        dl, wp, pmax,
        IM_COL32(28, 16, 52, (int)(255.0f * alpha)),
        radius, 0
    );

    ImDrawList_AddRectFilled(
        dl, wp, gc_v2(wp.x + rail_w, pmax.y),
        IM_COL32(10, 5, 25, (int)(130.0f * alpha)),
        radius, ImDrawFlags_RoundCornersLeft
    );

    ImDrawList_AddRect(
        dl, wp, pmax,
        IM_COL32(190, 160, 255, 105),
        radius, 0, 1.5f
    );

    /* While moving/resizing, make that obvious. */
    if (adjusting) {
        ImDrawList_AddRect(
            dl, wp, pmax,
            IM_COL32(93, 255, 154, 220),
            radius, 0, 2.5f
        );
    }

    /* ---------- side rail: Chat / Players / SOS / Settings ---------- */
    unsigned long unread =
        gc_total_messages - gc_seen_messages;

    float btn = rail_w * 0.68f;
    float rail_x = wp.x + (rail_w - btn) * 0.5f;
    float rail_y = wp.y + u * 0.9f;
    float rail_gap = u * 0.7f;

    static const char* const tab_tips[4] = {
        "Chat", "Players", "SOS", "Settings"
    };

    for (int t = 0; t < 4; t++) {
        ImVec2 bmn = gc_v2(
            rail_x,
            rail_y + (float)t * (btn + rail_gap)
        );

        bool on = gc_tab == t;

        if (on) {
            ImDrawList_AddRectFilled(
                dl,
                gc_add(bmn, -3.0f, -3.0f),
                gc_add(bmn, btn + 3.0f, btn + 3.0f),
                IM_COL32(124, 74, 232, 70),
                btn * 0.32f, 0
            );

            /* Accent bar on the panel edge marks the open tab. */
            ImDrawList_AddRectFilled(
                dl,
                gc_v2(wp.x + 2.0f, bmn.y + btn * 0.2f),
                gc_v2(wp.x + 5.0f, bmn.y + btn * 0.8f),
                IM_COL32(200, 175, 255, 255),
                2.0f, 0
            );
        }

        if (
            gc_button(
                gc_tab_ids[t],
                bmn,
                gc_v2(btn, btn),
                on ?
                    IM_COL32(124, 74, 232, 255) :
                    IM_COL32(255, 255, 255, 16),
                on ?
                    IM_COL32(140, 90, 245, 255) :
                    IM_COL32(255, 255, 255, 38),
                btn * 0.28f,
                0
            )
        ) {
            gc_tab = t;
        }

        if (igIsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            igSetTooltip("%s", tab_tips[t]);
        }

        ImVec2 c = gc_add(bmn, btn * 0.5f, btn * 0.5f);
        float is = btn * 0.52f;
        ImU32 ic = IM_COL32(255, 255, 255, 255);

        if (t == GC_TAB_CHAT) {
            gc_icon_chat(dl, c, is, ic);
        } else if (t == GC_TAB_PLAYERS) {
            gc_icon_players(dl, c, is, ic);
        } else if (t == GC_TAB_SOS) {
            gc_icon_sos(
                dl, c, is,
                (global_chat_is_sos_active() || others_sos > 0) ?
                    IM_COL32(255, 90, 74, 255) : ic,
                on ?
                    IM_COL32(124, 74, 232, 255) :
                    IM_COL32(44, 30, 78, 255)
            );
        } else {
            gc_icon_gear(dl, c, is, ic);
        }

        char nb[8];

        if (t == GC_TAB_CHAT && !on && unread > 0) {
            if (unread > 99) {
                snprintf(nb, sizeof(nb), "99+");
            } else {
                snprintf(nb, sizeof(nb), "%lu", unread);
            }

            gc_badge(dl, bmn, btn, u, nb, IM_COL32(255, 74, 94, 255));
        }

        if (t == GC_TAB_PLAYERS && is_connected && online_count > 0) {
            snprintf(
                nb, sizeof(nb), "%d",
                online_count > 99 ? 99 : online_count
            );

            gc_badge(dl, bmn, btn, u, nb, IM_COL32(40, 160, 90, 255));
        }

        if (t == GC_TAB_SOS && others_sos > 0) {
            snprintf(
                nb, sizeof(nb), "%d",
                others_sos > 99 ? 99 : others_sos
            );

            gc_badge(
                dl, bmn, btn, u, nb,
                IM_COL32(255, (int)(50.0f + pulse * 50.0f), 50, 255)
            );
        }
    }

    gc_qm_ensure_loaded();

    /* ---- rail extras: quick messages ---- */
    {
        float y = rail_y + 4.0f * (btn + rail_gap);
        float rail_bottom = pmax.y - u * 0.85f;

        /* Quick message buttons, as many as fit the rail's height. */
        float qh = btn * 0.72f;
        float qgap = rail_gap * 0.6f;

        /* SOS button pinned to the bottom of the rail (far from the close
         * button). Only drawn if the panel is tall enough for it. */
        if (y + qh <= rail_bottom) {
            bool sos_on_r = global_chat_is_sos_active();
            ImVec2 smn = gc_v2(rail_x, rail_bottom - qh);

            rail_bottom -= qh + qgap * 2.0f;

            if (
                gc_button(
                    "##gc_rail_sos", smn, gc_v2(btn, qh),
                    sos_on_r ?
                        IM_COL32(255, (int)(50.0f + pulse * 40.0f), 40, 255) :
                        IM_COL32(224, 56, 44, 255),
                    IM_COL32(245, 90, 76, 255),
                    qh * 0.3f, IM_COL32(255, 160, 150, 90)
                )
            ) {
                gc_toggle_sos();
            }

            gc_text_center(
                dl, smn, gc_v2(btn, qh),
                IM_COL32(255, 255, 255, 255), "SOS"
            );
        }

        if (y + qh <= rail_bottom) {
            ImDrawList_AddLine(
                dl,
                gc_v2(rail_x + btn * 0.15f, y - rail_gap * 0.5f),
                gc_v2(rail_x + btn * 0.85f, y - rail_gap * 0.5f),
                IM_COL32(190, 160, 255, 50),
                1.0f
            );
        }

        for (int q = 0; q < GC_QM_COUNT; q++) {
            if (gc_qm_text[q][0] == '\0' || gc_qm_label[q][0] == '\0') {
                continue;
            }

            if (y + qh > rail_bottom) {
                break;
            }

            ImVec2 qmn = gc_v2(rail_x, y);

            char qid[16];
            snprintf(qid, sizeof(qid), "##gc_qm%d", q);

            if (
                gc_button(
                    qid,
                    qmn,
                    gc_v2(btn, qh),
                    q == 0 ?
                        IM_COL32(224, 56, 44, 120) :
                        IM_COL32(124, 74, 232, 90),
                    q == 0 ?
                        IM_COL32(245, 90, 76, 190) :
                        IM_COL32(150, 100, 250, 170),
                    qh * 0.3f,
                    IM_COL32(190, 160, 255, 70)
                )
            ) {
                gc_send_quick(env, q);
            }

            gc_text_center(
                dl, qmn, gc_v2(btn, qh),
                IM_COL32(255, 255, 255, 255),
                gc_qm_label[q]
            );

            y += qh + qgap;
        }
    }

    if (gc_tab == GC_TAB_CHAT) {
        if (!gc_chat_viewing) {
            /* Chat just came into view: anything unread gets a divider. */
            gc_chat_viewing = true;

            if (gc_total_messages > gc_seen_messages) {
                gc_divider_active = true;
                gc_divider_scroll = true;
                gc_divider_after = gc_seen_messages;
            } else {
                gc_divider_active = false;
            }
        }

        gc_seen_messages = gc_total_messages;
    } else {
        gc_chat_viewing = false;
        gc_divider_active = false;
    }

    /* ---------- main area geometry ---------- */
    float mx = wp.x + rail_w;
    float mw = ws.x - rail_w;
    float pad = u * 0.95f;

    float head_h = gc_clampf(ws.y * 0.095f, u * 2.7f, u * 3.8f);
    float bottom_pad = u * 0.85f;
    float done_h = u * 2.4f;

    float content_bot = pmax.y - bottom_pad;

    if (adjusting) {
        content_bot -= done_h + u * 0.5f;
    }

    /* ---------- notice toast (system / joined / left) ---------- */
    if (gc_show_events && gc_event_count > 0) {
        long long left_ms = gc_toast_until_ms - global_chat_now_ms();

        if (left_ms > 0) {
            float a = 1.0f;

            if (left_ms < GC_TOAST_FADE_MS) {
                a = (float)left_ms / (float)GC_TOAST_FADE_MS;
            }

            const gc_event* ev = &gc_events[gc_event_count - 1];
            ImU32 dot =
                ev->kind == GC_EV_JOIN ? IM_COL32(125, 255, 176, 255) :
                ev->kind == GC_EV_LEAVE ? IM_COL32(242, 77, 77, 255) :
                ev->kind == GC_EV_RECONNECT ? IM_COL32(255, 204, 77, 255) :
                                              IM_COL32(120, 210, 255, 255);

            ImDrawList* tdl =
                igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
            float tlh = igGetTextLineHeight();
            float tw = (ws.x - rail_w) - pad * 2.0f;
            float th = tlh + u * 0.6f;
            ImVec2 tp = gc_v2(mx + pad, wp.y + head_h + u * 0.15f);

            char tt[96];
            gc_clip_text(tt, sizeof(tt), ev->text, tw - u * 2.4f);

            ImDrawList_AddRectFilled(
                tdl, tp, gc_add(tp, tw, th),
                IM_COL32(36, 22, 64, (int)(235.0f * a)), th * 0.5f, 0
            );
            ImDrawList_AddRect(
                tdl, tp, gc_add(tp, tw, th),
                IM_COL32(190, 160, 255, (int)(110.0f * a)), th * 0.5f, 0, 1.0f
            );
            ImDrawList_AddCircleFilled(
                tdl, gc_v2(tp.x + u * 0.9f, tp.y + th * 0.5f), u * 0.24f,
                (dot & 0x00FFFFFFu) | ((ImU32)(255.0f * a) << 24), 10
            );
            ImDrawList_AddText_Vec2(
                tdl, gc_v2(tp.x + u * 1.6f, tp.y + (th - tlh) * 0.5f),
                IM_COL32(222, 236, 255, (int)(255.0f * a)), tt, NULL
            );
        }
    }

    /* ---------- header ---------- */
    const char* title = "{ J S R } TEAM CHAT";
    float title_y = wp.y + head_h * 0.1f;
    float title_w;

    igPushFont(NULL, igGetStyle()->FontSizeBase * 0.85f * 1.12f);

    title_w = gc_text_w(title);

    ImDrawList_AddText_Vec2(
        dl,
        gc_v2(mx + pad, title_y),
        IM_COL32(241, 236, 255, 255),
        title,
        NULL
    );

    igPopFont();

    float status_end = mx + pad + title_w;

    {
        char status[64];

        if (is_connected) {
            snprintf(
                status, sizeof(status),
                "Connected  -  %d online", online_count
            );
        } else {
            snprintf(status, sizeof(status), "Not connected");
        }

        ImU32 scol =
            is_connected ?
                IM_COL32(125, 255, 176, 255) :
                IM_COL32(242, 77, 77, 255);

        float sy = title_y + u * 1.35f;

        ImDrawList_AddCircleFilled(
            dl,
            gc_v2(mx + pad + u * 0.3f, sy + u * 0.5f),
            u * 0.26f,
            scol,
            12
        );

        ImDrawList_AddText_Vec2(
            dl,
            gc_v2(mx + pad + u * 0.95f, sy),
            scol,
            status,
            NULL
        );

        float se = mx + pad + u * 0.95f + gc_text_w(status);

        if (se > status_end) {
            status_end = se;
        }
    }

    /* SOS pill + close (collapses back to the Open card) */
    {
        float hb = head_h * 0.62f;
        float by = wp.y + (head_h - hb) * 0.5f;
        float x_close = pmax.x - pad * 0.5f - hb;

        /* SOS is no longer a button up here (it sat right next to the close
         * button and got tapped by accident). While it is on, the time left
         * is shown as plain, non-tappable text; the SOS button lives at the
         * bottom of the rail and in the SOS tab. */
        if (global_chat_is_sos_active()) {
            long long secs = global_chat_sos_remaining_ms() / 1000;
            char sl[24];

            snprintf(
                sl, sizeof(sl), "SOS %lld:%02lld",
                secs / 60, secs % 60
            );

            float sw = gc_text_w(sl);
            float sx = x_close - u * 0.7f - sw;

            if (sx > status_end + u * 0.6f) {
                ImDrawList_AddText_Vec2(
                    dl, gc_v2(sx, wp.y + (head_h - u) * 0.5f),
                    IM_COL32(255, (int)(110.0f + pulse * 60.0f), 96, 255),
                    sl, NULL
                );
            }
        }

        if (
            gc_button(
                "##gc_close",
                gc_v2(x_close, by),
                gc_v2(hb, hb),
                IM_COL32(255, 255, 255, 14),
                IM_COL32(255, 255, 255, 40),
                hb * 0.5f, 0
            )
        ) {
            gc_close_requested = true;
        }

        gc_icon_x(
            dl, gc_v2(x_close + hb * 0.5f, by + hb * 0.5f),
            hb * 0.42f, IM_COL32(203, 182, 255, 255)
        );
    }

    ImDrawList_AddLine(
        dl,
        gc_v2(mx, wp.y + head_h),
        gc_v2(pmax.x - 1.0f, wp.y + head_h),
        IM_COL32(190, 160, 255, 34),
        1.0f
    );

    /* Soft glow along the header line. */
    ImDrawList_AddRectFilledMultiColor(
        dl,
        gc_v2(mx, wp.y + head_h - 1.0f),
        gc_v2(pmax.x - radius * 0.6f, wp.y + head_h + 1.0f),
        IM_COL32(190, 160, 255, 120),
        IM_COL32(190, 160, 255, 0),
        IM_COL32(190, 160, 255, 0),
        IM_COL32(190, 160, 255, 120)
    );

    /* ---------- tab content ---------- */
    float content_top = wp.y + head_h + u * 0.35f;
    float area_x = mx + pad * 0.6f;
    float area_w = mw - pad * 1.2f;

    float list_hit_w = u * 1.6f;
    float list_w = area_w - list_hit_w;

    if (list_w < 60.0f) {
        list_w = 60.0f;
    }

    if (needs_key) {
        gc_draw_key_screen(
            env, usrs, rejected,
            gc_v2(mx, content_top),
            gc_v2(mw, content_bot - content_top),
            u
        );
    } else if (gc_tab == GC_TAB_CHAT) {
        float in_h = gc_clampf(ws.y * 0.07f, u * 2.3f, u * 3.1f);
        float in_y = content_bot - in_h;
        float gap = u * 0.55f;

        /* Picked-emoji strip + emoji picker stack up above the input. */
        float gv = u * 0.45f;
        float ycur = in_y;

        float tray_h = gc_tray_n > 0 ? gc_tray_height(u) : 0.0f;
        float tray_y = 0.0f;

        if (tray_h > 0.0f) {
            ycur -= gv;
            tray_y = ycur - tray_h;
            ycur = tray_y;
        }

        float pick_h = 0.0f;
        float pick_y = 0.0f;
        float pick_chip_h = 0.0f;
        int pick_cols = 1;

        if (gc_emoji_picker_open) {
            gc_emoji_grid_layout(
                area_w, (ycur - gv - content_top) * 0.6f, u,
                NTL_EMOJI_COUNT, &pick_chip_h, &pick_cols, &pick_h
            );

            ycur -= gv;
            pick_y = ycur - pick_h;
            ycur = pick_y;
        }

        float msg_bot = ycur - gv;

        gc_draw_messages(
            env,
            gc_v2(area_x, content_top),
            gc_v2(area_w, msg_bot - content_top),
            u,
            !adjusting_pos
        );

        if (gc_emoji_picker_open) {
            gc_draw_emoji_grid(
                "##gc_emoji_picker",
                gc_v2(area_x, pick_y), gc_v2(area_w, pick_h),
                u, pick_chip_h, pick_cols, NULL
            );
        }

        if (tray_h > 0.0f) {
            gc_draw_tray(gc_v2(area_x, tray_y), area_w, u);
        }

        /* Message input + emoji + send */
        float in_w = mw - pad * 2.0f - (in_h + gap) * 2.0f;

        igSetCursorScreenPos(gc_v2(mx + pad, in_y));

        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.7f);
        igPushStyleVar_Vec2(
            ImGuiStyleVar_FramePadding,
            gc_v2(u * 0.9f, (in_h - u) * 0.5f)
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBgHovered, (ImVec4){1.0f, 1.0f, 1.0f, 0.15f}
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBgActive, (ImVec4){1.0f, 1.0f, 1.0f, 0.18f}
        );

        igPushItemWidth(in_w);

        bool input_ext = gc_has_non_ascii(global_chat_input);

        if (input_ext) gc_emoji_push();

        bool submitted =
            igInputTextWithHint(
                "##global_chat_input",
                "Type your message...",
                global_chat_input,
                sizeof(global_chat_input),
                ImGuiInputTextFlags_EnterReturnsTrue,
                NULL,
                NULL
            );

        if (input_ext) gc_emoji_pop();

        igPopItemWidth();
        igPopStyleColor(3);
        igPopStyleVar(2);

        /* Character counter when getting close to the limit. */
        {
            int used = (int)strlen(global_chat_input);
            int limit = (int)sizeof(global_chat_input) - 1;

            if (used >= limit - 40) {
                char cnt[16];
                snprintf(cnt, sizeof(cnt), "%d/%d", used, limit);

                float cw = gc_text_w(cnt);

                ImDrawList_AddText_Vec2(
                    dl,
                    gc_v2(
                        mx + pad + in_w - cw - u * 0.8f,
                        in_y + (in_h - u) * 0.5f
                    ),
                    used >= limit - 10 ?
                        IM_COL32(255, 120, 100, 255) :
                        IM_COL32(150, 142, 185, 230),
                    cnt, NULL
                );
            }
        }

        ImVec2 emn = gc_v2(mx + pad + in_w + gap, in_y);

        if (
            gc_button(
                "##gc_emoji_btn",
                emn,
                gc_v2(in_h, in_h),
                gc_emoji_picker_open ?
                    IM_COL32(124, 74, 232, 200) :
                    IM_COL32(255, 255, 255, 22),
                gc_emoji_picker_open ?
                    IM_COL32(150, 100, 250, 230) :
                    IM_COL32(255, 255, 255, 46),
                u * 0.7f,
                IM_COL32(190, 160, 255, 60)
            )
        ) {
            gc_emoji_picker_open = !gc_emoji_picker_open;
        }

        gc_icon_smiley(
            dl,
            gc_v2(emn.x + in_h * 0.5f, emn.y + in_h * 0.5f),
            in_h * 0.52f,
            IM_COL32(241, 236, 255, 255)
        );

        ImVec2 smn = gc_v2(mx + mw - pad - in_h, in_y);

        bool send_clicked =
            gc_button(
                "##gc_send",
                smn,
                gc_v2(in_h, in_h),
                IM_COL32(124, 74, 232, 255),
                IM_COL32(150, 100, 250, 255),
                u * 0.7f,
                0
            );

        gc_icon_send(
            dl,
            gc_v2(smn.x + in_h * 0.5f, smn.y + in_h * 0.5f),
            in_h * 0.38f,
            IM_COL32(255, 255, 255, 255)
        );

        if (submitted || send_clicked) {
            gc_tray_flush_into_input();
            gc_submit_message(env);
        }
    } else if (gc_tab == GC_TAB_PLAYERS) {
        float area_h = content_bot - content_top;

        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_players",
            gc_v2(list_w, area_h),
            0,
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar
        );

        char hdr[48];
        snprintf(hdr, sizeof(hdr), "Online players (%d)", online_count);

        ImVec2 hp;
        igGetCursorScreenPos(&hp);

        igTextColored((ImVec4){0.71f, 0.61f, 0.96f, 1.0f}, "%s", hdr);

        ImVec2 av;
        igGetContentRegionAvail(&av);

        {
            const char* hint = "Tap to highlight";
            float hw = gc_text_w(hint);

            if (hw + gc_text_w(hdr) + u * 2.0f < av.x) {
                ImDrawList_AddText_Vec2(
                    igGetWindowDrawList(),
                    gc_v2(hp.x + av.x - hw, hp.y),
                    IM_COL32(120, 112, 155, 200), hint, NULL
                );
            }
        }

        igDummy(gc_v2(1.0f, u * 0.2f));

        gc_draw_players_list(env, u, av.x, own_name);
        gc_draw_activity(u, av.x);

        gc_end_scroll_child(
            gc_v2(area_x, content_top), area_h,
            list_hit_w, list_w, u, !adjusting_pos, true
        );
    } else if (gc_tab == GC_TAB_SOS) {
        float area_h = content_bot - content_top;

        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_sos",
            gc_v2(list_w, area_h),
            0,
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar
        );

        bool sos_on = global_chat_is_sos_active();

        ImVec2 av;
        igGetContentRegionAvail(&av);

        float lh = igGetTextLineHeight();
        ImDrawList* cdl = igGetWindowDrawList();

        /* Status card */
        {
            ImVec2 sp;
            igGetCursorScreenPos(&sp);

            float card_h = lh * 4.0f;
            ImVec2 smax = gc_add(sp, av.x, card_h);

            ImDrawList_AddRectFilled(
                cdl, sp, smax,
                sos_on ?
                    IM_COL32(224, 56, 44, (int)(46.0f + pulse * 34.0f)) :
                    IM_COL32(255, 255, 255, 14),
                u * 0.8f, 0
            );
            ImDrawList_AddRect(
                cdl, sp, smax,
                sos_on ?
                    IM_COL32(255, 90, 74, 190) :
                    IM_COL32(190, 160, 255, 50),
                u * 0.8f, 0, 1.2f
            );

            gc_icon_sos(
                cdl, gc_v2(sp.x + u * 1.7f, sp.y + card_h * 0.5f),
                lh * 1.7f,
                sos_on ? IM_COL32(255, 90, 74, 255) : IM_COL32(190, 175, 235, 255),
                sos_on ? IM_COL32(60, 20, 20, 255) : IM_COL32(44, 30, 78, 255)
            );

            float tx = sp.x + u * 3.4f;
            float room = smax.x - u * 0.7f - tx;
            char l1[48];
            char l2[96];
            char l2c[96];

            if (sos_on) {
                snprintf(l1, sizeof(l1), "SOS is ON");

                long long secs = global_chat_sos_remaining_ms() / 1000;
                snprintf(
                    l2, sizeof(l2), "%lld:%02lld left", secs / 60, secs % 60
                );
            } else {
                snprintf(l1, sizeof(l1), "SOS is off");
                snprintf(
                    l2, sizeof(l2), "Alerts %d online teammate%s",
                    online_count > 1 ? online_count - 1 : 0,
                    online_count == 2 ? "" : "s"
                );
            }

            ImDrawList_AddText_Vec2(
                cdl, gc_v2(tx, sp.y + lh * 0.7f),
                sos_on ? IM_COL32(255, 150, 138, 255) : IM_COL32(241, 236, 255, 255),
                l1, NULL
            );

            gc_clip_text(l2c, sizeof(l2c), l2, room);

            ImDrawList_AddText_Vec2(
                cdl, gc_v2(tx, sp.y + lh * 1.8f),
                IM_COL32(170, 160, 205, 230), l2c, NULL
            );

            /* Time-left bar (5 minute SOS). */
            float bar_y = sp.y + lh * 3.0f;
            float bar_h = fmaxf(4.0f, u * 0.35f);
            float frac =
                sos_on ?
                    gc_clampf(
                        (float)global_chat_sos_remaining_ms() / 300000.0f,
                        0.0f, 1.0f
                    ) :
                    0.0f;

            ImDrawList_AddRectFilled(
                cdl, gc_v2(tx, bar_y), gc_v2(tx + room, bar_y + bar_h),
                IM_COL32(255, 255, 255, 22), bar_h * 0.5f, 0
            );

            if (frac > 0.0f) {
                ImDrawList_AddRectFilled(
                    cdl, gc_v2(tx, bar_y),
                    gc_v2(tx + room * frac, bar_y + bar_h),
                    IM_COL32(255, 90, 74, 255), bar_h * 0.5f, 0
                );
            }

            igDummy(gc_v2(av.x, card_h + u * 0.5f));
        }

        ImVec2 p;
        igGetCursorScreenPos(&p);

        float bh = u * 3.0f;

        if (
            gc_button(
                "##gc_sos_big",
                p,
                gc_v2(av.x, bh),
                sos_on ?
                    IM_COL32(120, 40, 36, 255) :
                    IM_COL32(224, 56, 44, 255),
                sos_on ?
                    IM_COL32(150, 55, 48, 255) :
                    IM_COL32(245, 90, 76, 255),
                u * 0.8f, 0
            )
        ) {
            gc_toggle_sos();
        }

        ImDrawList_AddRectFilled(
            cdl, p, gc_v2(p.x + av.x, p.y + bh * 0.5f),
            IM_COL32(255, 255, 255, 22), u * 0.8f,
            ImDrawFlags_RoundCornersTop
        );

        gc_text_center(
            cdl, p, gc_v2(av.x, bh),
            IM_COL32(255, 255, 255, 255),
            sos_on ? "CANCEL SOS" : "SEND SOS (5 min)"
        );

        igSetCursorScreenPos(p);
        igDummy(gc_v2(av.x, bh + u * 0.5f));

        /* SOS beep on/off (tap while turning on to hear a test beep). */
        {
            ImVec2 bp;
            igGetCursorScreenPos(&bp);

            float tbh = u * 2.3f;

            if (
                gc_pill_button(
                    "##gc_sos_sound", bp, gc_v2(av.x, tbh),
                    gc_sos_sound ? "SOS BEEP: ON" : "SOS BEEP: OFF",
                    gc_sos_sound, u
                )
            ) {
                gc_sos_sound = !gc_sos_sound;
                gc_qm_save();

                if (gc_sos_sound) {
                    gc_play_sos_beep(0);
                }
            }

            igSetCursorScreenPos(bp);
            igDummy(gc_v2(av.x, tbh + u * 0.6f));
        }

        gc_section("Teammates asking for help", av.x, u);

        if (gc_draw_sos_cards(u, av.x, own_name) == 0) {
            ImVec2 ep;
            igGetCursorScreenPos(&ep);

            float eh = lh * 3.0f;

            ImDrawList_AddRectFilled(
                cdl, ep, gc_add(ep, av.x, eh),
                IM_COL32(125, 255, 176, 14), u * 0.8f, 0
            );

            gc_text_center(
                cdl, ep, gc_v2(av.x, eh),
                IM_COL32(125, 255, 176, 190),
                "All clear - nobody needs help"
            );

            igDummy(gc_v2(av.x, eh));
        }

        gc_end_scroll_child(
            gc_v2(area_x, content_top), area_h,
            list_hit_w, list_w, u, !adjusting_pos, false
        );
    } else {
        /* Settings */
        float area_h = content_bot - content_top;

        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_settings",
            gc_v2(list_w, area_h),
            0,
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar
        );

        ImVec2 av;
        igGetContentRegionAvail(&av);

        float bh = u * 2.5f;
        float gap = u * 0.5f;
        float bw = (av.x - gap) * 0.5f;

        ImU32 dim = IM_COL32(150, 142, 185, 220);

        /* ---- window ---- */
        gc_section("Window", av.x, u);

        ImVec2 p;
        igGetCursorScreenPos(&p);

        if (
            gc_pill_button(
                "##gc_s_move", p, gc_v2(bw, bh),
                adjusting_pos ? "DONE MOVING" : "MOVE",
                adjusting_pos, u
            )
        ) {
            gc_toggle_adjust(env, GLOBAL_CHAT_ADJUST_POSITION);
        }

        ImVec2 p2 = gc_v2(p.x + bw + gap, p.y);

        if (
            gc_pill_button(
                "##gc_s_size", p2, gc_v2(bw, bh),
                adjusting_size ? "DONE SIZING" : "RESIZE",
                adjusting_size, u
            )
        ) {
            gc_toggle_adjust(env, GLOBAL_CHAT_ADJUST_SIZE);
        }

        igSetCursorScreenPos(p);
        igDummy(gc_v2(av.x, bh + u * 0.5f));

        if (adjusting_pos) {
            igTextWrapped(
                "Drag the chat anywhere, then press DONE MOVING."
            );
        } else if (adjusting_size) {
            igTextWrapped(
                "Drag the bottom-right corner, then press DONE SIZING."
            );
        }

        igDummy(gc_v2(1.0f, u * 0.3f));

        /* ---- appearance ---- */
        gc_section("Panel opacity", av.x, u);

        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.5f);
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
        );
        igPushItemWidth(-1.0f);

        igSliderFloat(
            "##gc_alpha",
            &gc_panel_alpha,
            0.10f,
            1.0f,
            "%.2f",
            0
        );

        igPopItemWidth();
        igPopStyleColor(1);
        igPopStyleVar(1);

        igDummy(gc_v2(1.0f, u * 0.3f));

        /* ---- chat ---- */
        gc_section("Chat", av.x, u);

        igGetCursorScreenPos(&p);

        if (
            gc_pill_button(
                "##gc_s_events", p, gc_v2(av.x, bh),
                gc_show_events ? "NOTICES: ON" : "NOTICES: OFF",
                gc_show_events, u
            )
        ) {
            gc_show_events = !gc_show_events;
        }

        igSetCursorScreenPos(p);
        igDummy(gc_v2(av.x, bh + u * 0.15f));

        {
            ImVec2 cp;
            igGetCursorScreenPos(&cp);

            char cap_c[64];
            gc_clip_text(
                cap_c, sizeof(cap_c),
                "Show join / leave messages in chat", av.x
            );

            ImDrawList_AddText_Vec2(
                igGetWindowDrawList(), cp, dim, cap_c, NULL
            );

            igDummy(gc_v2(av.x, igGetTextLineHeight() + u * 0.4f));
        }

        /* ---- profile emoji ---- */
        gc_section("Profile emoji", av.x, u);

        {
            ImVec2 ep;
            igGetCursorScreenPos(&ep);

            float chip_h;
            int cols;
            float full_h;

            gc_emoji_grid_layout(
                av.x, 1.0e9f, u, NTL_EMOJI_COUNT + 1,
                &chip_h, &cols, &full_h
            );

            /* About three rows visible, with a peek of the next one. */
            float view_h =
                u * 1.1f + chip_h * 3.45f + u * 0.9f;

            if (view_h > full_h) {
                view_h = full_h;
            }

            gc_draw_emoji_grid(
                "##gc_pf_grid", ep, gc_v2(av.x, view_h),
                u, chip_h, cols, usrs
            );

            igSetCursorScreenPos(ep);
            igDummy(gc_v2(av.x, view_h + u * 0.5f));
        }

        /* ---- quick messages editor ---- */
        gc_section("Quick messages", av.x, u);

        igTextDisabled("Button label  |  message it sends");

        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.5f);
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
        );

        for (int q = 0; q < GC_QM_COUNT; q++) {
            igPushID_Int(q);

            igPushItemWidth(u * 5.0f);

            igInputTextWithHint(
                "##qml",
                "Label",
                gc_qm_label[q],
                GC_QM_LABEL_LEN,
                ImGuiInputTextFlags_None,
                NULL,
                NULL
            );

            if (igIsItemDeactivatedAfterEdit()) {
                gc_qm_save();
            }

            igPopItemWidth();

            igSameLine(0.0f, u * 0.4f);

            igPushItemWidth(-1.0f);

            igInputTextWithHint(
                "##qmt",
                "Message",
                gc_qm_text[q],
                GC_QM_TEXT_LEN,
                ImGuiInputTextFlags_None,
                NULL,
                NULL
            );

            if (igIsItemDeactivatedAfterEdit()) {
                gc_qm_save();
            }

            igPopItemWidth();

            igPopID();
        }

        igPopStyleColor(1);
        igPopStyleVar(1);

        igDummy(gc_v2(1.0f, u * 0.3f));

        /* Sound on/off + restore defaults */
        {
            ImVec2 sp;
            igGetCursorScreenPos(&sp);

            float sgap = u * 0.5f;
            float sbw = (av.x - sgap) * 0.5f;
            float sbh = u * 2.3f;

            if (
                gc_pill_button(
                    "##gc_qm_sound", sp, gc_v2(sbw, sbh),
                    gc_qm_sound ? "SOUND: ON" : "SOUND: OFF",
                    gc_qm_sound, u
                )
            ) {
                gc_qm_sound = !gc_qm_sound;
                gc_qm_save();

                if (gc_qm_sound) {
                    gc_play_quick_sound(1);
                }
            }

            ImVec2 rp = gc_v2(sp.x + sbw + sgap, sp.y);

            if (
                gc_pill_button(
                    "##gc_qm_reset", rp, gc_v2(sbw, sbh),
                    "RESET TEXTS", false, u
                )
            ) {
                bool keep_sound = gc_qm_sound;
                gc_qm_defaults();
                gc_qm_sound = keep_sound;
                gc_qm_save();
            }

            igSetCursorScreenPos(sp);
            igDummy(gc_v2(av.x, sbh + u * 0.5f));
        }

        gc_end_scroll_child(
            gc_v2(area_x, content_top), area_h,
            list_hit_w, list_w, u, !adjusting_pos, false
        );
    }

    /* ---------- DONE bar while moving / resizing (any tab) ---------- */
    if (adjusting) {
        ImVec2 dmn = gc_v2(mx + pad, pmax.y - bottom_pad - done_h);
        ImVec2 dsz = gc_v2(mw - pad * 2.0f, done_h);

        if (
            gc_button(
                "##gc_done", dmn, dsz,
                IM_COL32(40, 160, 90, 255),
                IM_COL32(55, 190, 110, 255),
                u * 0.7f, 0
            )
        ) {
            gc_toggle_adjust(env, global_chat_adjust);
        }

        gc_text_center(
            dl, dmn, dsz, IM_COL32(255, 255, 255, 255),
            adjusting_pos ? "DONE MOVING" : "DONE SIZING"
        );
    }

    igPopFont();
}

/*
 * Draws a dot for every other Public Chat player who is on
 * the SAME game server as us (positions from different
 * servers aren't in the same coordinate space, so they
 * can't be meaningfully compared). Meant to be called
 * right after ntl_team_draw_minimap() with the exact same
 * x/y/size, so the dots line up on the same circle.
 */
void global_chat_draw_minimap_markers(
    tenv* env,
    float x,
    float y,
    float size
) {
    if (
        env == NULL ||
        env->usr == NULL ||
        size <= 0.0f ||
        global_chat_net == NULL
    ) {
        return;
    }

    tuser_data* u = env->usr;
    game_data* g = &u->gdata;

    if (!u->usrs.ntl_show_teammates) {
        return;
    }

    if (
        g->conn != CONNECTED ||
        g->data.grd <= 0.0f
    ) {
        return;
    }

    ImDrawList* dl = igGetWindowDrawList();

    if (dl == NULL) {
        return;
    }

    float radius = size * 0.5f;
    float map_radius = radius * 0.90f;
    ImVec2 center = {
        x + radius,
        y + radius
    };

    int count =
        jsr_network_location_count(
            global_chat_net
        );

    for (
        int i = 0;
        i < count;
        i++
    ) {
        char srv[64];
        char pname[32];
        float lx;
        float ly;
        int shape;
        float cr, cg, cb;
        bool sos;

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                pname,
                sizeof(pname),
                srv,
                sizeof(srv),
                &lx,
                &ly,
                &shape,
                &cr,
                &cg,
                &cb,
                NULL,
                NULL,
                &sos,
                NULL
            )
        ) {
            continue;
        }

        /* Each teammate's own chosen marker color/shape --
         * see jsr_network_send_location -- rather than a
         * single local override, so e.g. a blue diamond they
         * picked shows up as a blue diamond here. */
        ImU32 col =
            igColorConvertFloat4ToU32(
                (ImVec4){cr, cg, cb, 1.0f}
            );

        if (
            strcmp(srv, u->usrs.ipv4) != 0
        ) {
            /* Different game server -- not comparable. */
            continue;
        }

        float rx =
            (lx - g->data.grd) / g->data.flux_grd;

        float ry =
            (ly - g->data.grd) / g->data.flux_grd;

        float dist =
            sqrtf(rx * rx + ry * ry);

        if (dist > 1.0f) {
            rx /= dist;
            ry /= dist;
        }

        ImVec2 p = {
            center.x + rx * map_radius,
            center.y + ry * map_radius
        };

        ntl_draw_marker(
            dl,
            p,
            u->usrs.ntl_marker_size,
            shape,
            col
        );

        if (sos) {
            /* SOS glow: a compact, softly pulsing red halo hugging the
             * dot (layered translucent discs + a thin ring). Small on
             * purpose, so it never covers the map. */
            float t = (float)global_chat_now_ms() * 0.001f;
            float m = u->usrs.ntl_marker_size;
            float pulse = 0.5f + 0.5f * sinf(t * 5.0f);
            float grow = 1.0f + 0.25f * pulse;

            for (int k = 0; k < 4; k++) {
                float r = m + (1.5f + (float)(3 - k) * 2.0f) * grow;

                ImDrawList_AddCircleFilled(
                    dl, p, r,
                    IM_COL32(255, 50, 36, (int)(26.0f + pulse * 18.0f)),
                    24
                );
            }

            ImDrawList_AddCircle(
                dl, p, m + 2.0f + pulse,
                IM_COL32(255, 80, 64, (int)(170.0f + pulse * 85.0f)),
                0, 1.6f
            );
        }

        if (u->usrs.ntl_marker_labels) {
            igPushFont(
                u->imgui_data.mono_font[FONT_SIZE_SMALL],
                u->imgui_data.mono_font[
                    FONT_SIZE_SMALL
                ]->LegacySize
            );

            ImVec2 text_size;

            igCalcTextSize(
                &text_size,
                pname,
                NULL,
                false,
                -1.0f
            );

            ImVec2 text_pos = {
                p.x - text_size.x * 0.5f,
                p.y + u->usrs.ntl_marker_size + 2.0f
            };

            ImDrawList_AddText_Vec2(
                dl,
                text_pos,
                IM_COL32(255, 255, 255, 225),
                pname,
                NULL
            );

            igPopFont();
        }
    }
}

jsr_network* global_chat_get_network(void) {
    return global_chat_net;
}

bool global_chat_is_teammate(
    const char* nickname
) {
    if (
        global_chat_net == NULL ||
        nickname == NULL ||
        nickname[0] == '\0'
    ) {
        return false;
    }

    int count =
        jsr_network_roster_count(
            global_chat_net
        );

    for (
        int i = 0;
        i < count;
        i++
    ) {
        char name[32];

        if (
            !jsr_network_roster_name(
                global_chat_net,
                i,
                name,
                sizeof(name)
            )
        ) {
            continue;
        }

        if (
            strcmp(name, nickname) == 0
        ) {
            return true;
        }
    }

    return false;
}

int global_chat_get_teammates(
    tenv* env,
    global_chat_teammate* out_teammates,
    int max_count
) {
    if (
        global_chat_net == NULL ||
        env == NULL ||
        env->usr == NULL ||
        out_teammates == NULL ||
        max_count <= 0
    ) {
        return 0;
    }

    const char* own_server_ip = env->usr->usrs.ipv4;

    int count =
        jsr_network_location_count(
            global_chat_net
        );

    int written = 0;

    for (
        int i = 0;
        i < count && written < max_count;
        i++
    ) {
        char username[32];
        char server_ip[64];
        int shape;
        float r, g, b;
        int score;
        int ping;
        bool sos;
        int emoji_id;

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                username,
                sizeof(username),
                server_ip,
                sizeof(server_ip),
                NULL,
                NULL,
                &shape,
                &r,
                &g,
                &b,
                &score,
                &ping,
                &sos,
                &emoji_id
            )
        ) {
            continue;
        }

        /* Only players we're actually teamed up with (authenticated
         * on the shared relay key), on the same game server as us --
         * positions/scores from a different match aren't meaningful
         * to compare against, same filter used for the minimap
         * markers. Distance is deliberately NOT filtered here. */
        if (
            !global_chat_is_teammate(username) ||
            strcmp(server_ip, own_server_ip) != 0
        ) {
            continue;
        }

        strncpy(
            out_teammates[written].nickname,
            username,
            sizeof(out_teammates[written].nickname) - 1
        );
        out_teammates[written].nickname[
            sizeof(out_teammates[written].nickname) - 1
        ] = '\0';
        out_teammates[written].shape = shape;
        out_teammates[written].color[0] = r;
        out_teammates[written].color[1] = g;
        out_teammates[written].color[2] = b;
        out_teammates[written].score = score;
        out_teammates[written].ping = ping;
        out_teammates[written].sos = sos;
        out_teammates[written].emoji_id = emoji_id;
        written++;
    }

    return written;
}

void global_chat_destroy(tenv* env) {
    (void)env;

    global_chat_initialized =
        false;

    global_chat_open =
        false;

    global_chat_message_count =
        0;

    gc_event_count = 0;
    gc_toast_until_ms = 0;

    memset(
        global_chat_input,
        0,
        sizeof(global_chat_input)
    );

    if (global_chat_net != NULL) {
        jsr_network_destroy(global_chat_net);
        global_chat_net = NULL;
    }

    if (global_chat_tchat != NULL) {
        tchat_destroy(global_chat_tchat);
        global_chat_tchat = NULL;
    }
}
