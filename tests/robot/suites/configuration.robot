*** Settings ***
Documentation       Configuring a System B device headlessly: catalog device in a project, group links,
...                 generated download (specs/09) to a simulated device, and reading it back into a second
...                 project. Product file: the synthetic M-FFFF fixture (ADR-009). The tests run in order.
Resource            ../resources/kestrel.resource
Library             OperatingSystem

Suite Setup         Prepare
Suite Teardown      Stop Simulator
Test Tags           NFR-5


*** Variables ***
${WORK}         ${OUTPUT DIR}${/}configuration_work
${KNXPROD}      ${DATA}/catalog/M-FFFF.knxprod
${PRODUCT}      M-FFFF_H-KTB2-1_P-KTB.2D2
${APP}          M-FFFF_A-0001-10-0001


*** Test Cases ***
Add Catalog Device And Link Group Addresses
    [Tags]    FR-TOP-2    FR-GA-3
    ${area}=    Run Cli For Id    topology    add-area    ${DIR}    1    Home
    ${line}=    Run Cli For Id    topology    add-line    ${DIR}    ${area}    1    Main
    ${device}=    Run Cli For Id    topology    add-device    ${DIR}    ${line}    Test button    --number    10
    ...    --product    ${PRODUCT}    --application    ${APP}
    Set Suite Variable    ${DEVICE}    ${device}
    ${link}=    Run Cli Json    device    link    ${DIR}    ${device}    1    1/0/6    --knxprod    ${KNXPROD}
    Should Be Equal As Integers    ${link}[object]    1
    Run Cli Json    device    link    ${DIR}    ${device}    2    2/0/6    --knxprod    ${KNXPROD}
    ${info}=    Run Cli Json    project    info    ${DIR}
    Length Should Be    ${info}[comObjects]    2
    Length Should Be    ${info}[links]    2

Unknown Com Object Is Refused
    [Tags]    FR-GA-3
    ${result}=    Run Cli Json    device    link    ${DIR}    ${DEVICE}    99    1/0/7    --knxprod    ${KNXPROD}
    ...    expected_rc=1
    Should Not Be True    ${result}[ok]

Dry Run Plans A System B Download
    [Tags]    FR-COM-2
    ${plan}=    Run Cli Json    project    download    ${DIR}    ${DEVICE}    --knxprod    ${KNXPROD}    --dry-run
    ...    --save-images    ${WORK}${/}images
    Should Be Equal    ${plan}[address]    1.1.10
    Should Be Equal    ${plan}[steps][0]    Authorize (default key)
    Should Be Equal    ${plan}[steps][-1]    Restart
    ${table}=    Get Binary File    ${WORK}${/}images${/}obj1.bin
    Should Be Equal As Strings    ${table}    ${{bytes.fromhex("0002 0806 1006")}}

Download To Simulated Device
    [Documentation]    The generated configuration reaches the device and every segment is loaded.
    [Tags]    FR-COM-2
    ${result}=    Run Cli Json    project    download    ${DIR}    ${DEVICE}    --knxprod    ${KNXPROD}
    ...    --interface    ${IF}    timeout=120
    Should Be True    ${result}[ok]
    ${objects}=    Run Cli Json    device    objects    1.1.10    --interface    ${IF}
    FOR    ${index}    IN RANGE    1    5
        Should Be Equal    ${objects}[objects][${index}][loadState]    loaded
    END
    ${memory}=    Run Cli Json    device    memory    1.1.10    0x1002    6    --interface    ${IF}
    Should Be Equal    ${memory}[data]    00 02 08 06 10 06

Import Device Into Another Project
    [Documentation]    Reading the device back gives the same com objects and links.
    [Tags]    FR-DEV-3
    ${copy}=    Set Variable    ${WORK}${/}copy
    Run Cli Json    project    new    ${copy}    --name    Copy
    ${import}=    Run Cli Json    device    import    1.1.10    --knxprod    ${KNXPROD}    --project    ${copy}
    ...    --interface    ${IF}    timeout=120
    Should Be Equal As Integers    ${import}[comObjects]    2
    Should Be Equal As Integers    ${import}[links]    2
    ${again}=    Run Cli Json    project    download    ${copy}    ${import}[device]    --knxprod    ${KNXPROD}
    ...    --dry-run    --save-images    ${WORK}${/}images-copy
    FOR    ${object}    IN    1    2    3    4
        ${a}=    Get Binary File    ${WORK}${/}images${/}obj${object}.bin
        ${b}=    Get Binary File    ${WORK}${/}images-copy${/}obj${object}.bin
        Should Be Equal    ${a}    ${b}
    END


*** Keywords ***
Prepare
    Remove Directory    ${WORK}    recursive=True
    Create Directory    ${WORK}
    Set Suite Variable    ${DIR}    ${WORK}${/}house
    Run Cli Json    project    new    ${DIR}    --name    Configuration test
    ${port}=    Start Simulator    ${DATA}/sim/systemb.json
    Set Suite Variable    ${IF}    127.0.0.1:${port}
