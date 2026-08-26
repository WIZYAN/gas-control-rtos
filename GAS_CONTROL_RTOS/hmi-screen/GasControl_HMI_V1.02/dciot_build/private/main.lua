-- 参数设置页面控件通知脚本。
-- MCU主动刷新文本不会触发本回调，只有人员输入或点击恢复默认才进入独立确认画面。
function on_control_notify(screen, control, value)
    if screen == 4 then
        if ((control >= 80) and (control <= 90)) or ((control == 97) and (value == 1)) then
            change_screen(5)
        end
    elseif screen == 5 then
        if ((control == 108) or (control == 109)) and (value == 1) then
            change_screen(4)
        end
    end
end
