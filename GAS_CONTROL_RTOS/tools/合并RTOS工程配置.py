from copy import deepcopy
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET


SCRIPT_DIR = Path(__file__).resolve().parent
RTOS_PROJECT = SCRIPT_DIR.parent
WORKSPACE = RTOS_PROJECT.parent
BARE_PROJECT = WORKSPACE / "GAS_CONTROL"


def find_option(root: ET.Element, key: str) -> ET.Element:
    """
    函数名：find_option。
    说明：在FSP配置XML中查找指定key的工程选项。
    输入：root为XML根节点；key为选项名称。
    输出：返回找到的option节点；未找到时抛出异常。
    """
    node = root.find(f"./generalSettings/option[@key='{key}']")
    if node is None:
        raise RuntimeError(f"未找到FSP选项：{key}")
    return node


def merge_fsp_configuration() -> None:
    """
    函数名：merge_fsp_configuration。
    说明：以原工程硬件配置为主体，合并新工程的FreeRTOS内核和端口配置。
    输入：无。
    输出：无；更新RTOS工程的configuration.xml。
    """
    bare_tree = ET.parse(BARE_PROJECT / "configuration.xml")
    rtos_tree = ET.parse(RTOS_PROJECT / "configuration.xml")
    bare_root = bare_tree.getroot()
    rtos_root = rtos_tree.getroot()

    find_option(bare_root, "#RTOS#").set("value", "rtos.awsfreertos")
    bare_root.set("version", rtos_root.get("version", bare_root.get("version", "3")))

    bare_components = bare_root.find("./raComponentSelection")
    rtos_components = rtos_root.find("./raComponentSelection")
    if bare_components is None or rtos_components is None:
        raise RuntimeError("FSP组件配置缺失")

    for component in list(bare_components.findall("component")):
        if component.get("class") == "RTOS" or component.get("subgroup") == "rm_freertos_port":
            bare_components.remove(component)
    for component in rtos_components.findall("component"):
        if component.get("class") == "RTOS" or component.get("subgroup") == "rm_freertos_port":
            bare_components.append(deepcopy(component))

    bare_modules = bare_root.find("./raModuleConfiguration")
    rtos_modules = rtos_root.find("./raModuleConfiguration")
    if bare_modules is None or rtos_modules is None:
        raise RuntimeError("FSP模块配置缺失")

    for node in list(bare_modules):
        if node.get("id") in ("module.middleware.rm_freertos_port.0", "config.awsfreertos.thread"):
            bare_modules.remove(node)
    port_module = rtos_modules.find("./module[@id='module.middleware.rm_freertos_port.0']")
    thread_config = rtos_modules.find("./config[@id='config.awsfreertos.thread']")
    if port_module is None or thread_config is None:
        raise RuntimeError("RTOS模板中没有FreeRTOS端口或线程配置")
    bare_modules.insert(0, deepcopy(port_module))
    bare_modules.append(deepcopy(thread_config))

    hal_context = bare_modules.find("./context[@id='_hal.0']")
    if hal_context is None:
        raise RuntimeError("原工程缺少HAL上下文")
    if hal_context.find("./stack[@module='module.middleware.rm_freertos_port.0']") is None:
        hal_context.append(ET.Element("stack", {"module": "module.middleware.rm_freertos_port.0"}))

    main_stack = bare_root.find("./raBspConfiguration/config[@id='config.bsp.ra']/property[@id='config.bsp.common.main']")
    if main_stack is not None:
        main_stack.set("value", "0x400")

    ET.indent(bare_tree, space="  ")
    bare_tree.write(RTOS_PROJECT / "configuration.xml", encoding="UTF-8", xml_declaration=True)


def merge_cproject() -> None:
    """
    函数名：merge_cproject。
    说明：在RTOS工程的Debug和Release配置中增加业务模块包含路径与源码目录。
    输入：无。
    输出：无；更新RTOS工程.cproject。
    """
    cproject_path = RTOS_PROJECT / ".cproject"
    tree = ET.parse(cproject_path)
    root = tree.getroot()
    source_names = ["AT24C256", "CAN", "HMI", "MyUnitFile", "modbus", "modbus_poll"]
    include_names = ["MyUnitFile", "modbus", "HMI", "AT24C256", "CAN", "modbus_poll"]

    for option in root.findall(".//option"):
        if option.get("superClass") == "ilg.gnuarmeclipse.managedbuild.cross.option.c.compiler.include.paths":
            for item in list(option.findall("listOptionValue")):
                item_value = item.get("value", "")
                if item_value.startswith("&quot;") and any(f"/{name}}}" in item_value for name in include_names):
                    option.remove(item)
                    # 清理早期迁移工具产生的双重XML转义路径。
            existing = {item.get("value", "") for item in option.findall("listOptionValue")}
            for name in include_names:
                value = f'"${{workspace_loc:/${{ProjName}}/{name}}}"'
                if value not in existing:
                    option.append(ET.Element("listOptionValue", {"builtIn": "false", "value": value}))

    for source_entries in root.findall(".//sourceEntries"):
        existing = {entry.get("name", "") for entry in source_entries.findall("entry")}
        for name in source_names:
            if name not in existing:
                source_entries.insert(0, ET.Element("entry", {
                    "flags": "VALUE_WORKSPACE_PATH|RESOLVED", "kind": "sourcePath", "name": name
                }))

    ET.indent(tree, space="\t")
    tree.write(cproject_path, encoding="UTF-8", xml_declaration=True)
    cproject_text = cproject_path.read_text(encoding="UTF-8")
    if "<?fileVersion 4.0.0?>" not in cproject_text:
        declaration_end = cproject_text.find("?>")
        if declaration_end < 0:
            raise RuntimeError(".cproject缺少XML声明")
        cproject_text = (cproject_text[:declaration_end + 2] + "\n<?fileVersion 4.0.0?>" +
                         cproject_text[declaration_end + 2:])
        cproject_path.write_text(cproject_text, encoding="UTF-8")
        # Eclipse CDT依赖fileVersion处理指令识别托管构建工程，缺失时FSP编辑器会出现空指针异常。


def merge_secure_configuration() -> None:
    """
    函数名：merge_secure_configuration。
    说明：同步FSP隐藏安全配置中的板级时钟、引脚和资源分区，并保留FreeRTOS工程标识。
    输入：无。
    输出：无；更新RTOS工程的.secure_xml和.secure_azone伴随配置。
    """
    secure_tree = ET.parse(BARE_PROJECT / ".secure_xml")
    secure_root = secure_tree.getroot()
    find_option(secure_root, "#RTOS#").set("value", "rtos.awsfreertos")
    ET.indent(secure_tree, space="  ")
    secure_tree.write(RTOS_PROJECT / ".secure_xml", encoding="UTF-8", xml_declaration=True)
    shutil.copy2(BARE_PROJECT / ".secure_azone", RTOS_PROJECT / ".secure_azone")


def copy_sources_and_generated_hardware() -> None:
    """
    函数名：copy_sources_and_generated_hardware。
    说明：复制原工程业务源码、管脚、FSP硬件实例和驱动，保留RTOS工程已有内核文件。
    输入：无。
    输出：无。
    """
    for name in ("AT24C256", "CAN", "HMI", "MyUnitFile", "modbus", "modbus_poll"):
        shutil.copytree(BARE_PROJECT / name, RTOS_PROJECT / name, dirs_exist_ok=True)

    shutil.copy2(BARE_PROJECT / "R7FA4M1AB3CFP.pincfg", RTOS_PROJECT / "R7FA4M1AB3CFP.pincfg")
    shutil.copytree(BARE_PROJECT / "ra_gen", RTOS_PROJECT / "ra_gen", dirs_exist_ok=True)
    shutil.copytree(BARE_PROJECT / "ra_cfg" / "fsp_cfg", RTOS_PROJECT / "ra_cfg" / "fsp_cfg", dirs_exist_ok=True)
    shutil.copytree(BARE_PROJECT / "ra" / "fsp", RTOS_PROJECT / "ra" / "fsp", dirs_exist_ok=True)


def main() -> None:
    """
    函数名：main。
    说明：执行裸机工程到FreeRTOS工程的配置和源码基础合并。
    输入：无。
    输出：无。
    """
    merge_fsp_configuration()
    merge_cproject()
    merge_secure_configuration()
    copy_sources_and_generated_hardware()
    print("已完成FreeRTOS与原工程硬件配置合并。")


if __name__ == "__main__":
    main()
