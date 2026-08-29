-- 参数确认、日志清除和日志条件查询页面跳转脚本。
-- MCU主动刷新不触发本回调；只处理人员触摸事件。
function on_control_notify(screen, control, value)
    if screen == 4 then
        if ((control >= 80) and (control <= 90)) or ((control == 97) and (value == 1)) then
            change_screen(5)
        elseif (control == 142) and (value == 1) then
            change_screen(7)
        end
    elseif screen == 5 then
        if ((control == 108) or (control == 109)) and (value == 1) then
            change_screen(4)
        end
    elseif screen == 6 then
        if (control == 136) and (value == 1) then
            change_screen(2)
        elseif (control == 137) and (value == 1) then
            change_screen(3)
        end
    elseif screen == 7 then
        if (control == 147) and (value == 1) then
            change_screen(4)
        end
    elseif screen == 2 then
        if (control == 69) and (value == 1) then
            change_screen(3)
        end
    elseif screen == 3 then
        if (control == 70) and (value == 1) then
            change_screen(2)
        end
    end
end
