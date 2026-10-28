*** Settings ***
Documentation       Project editing with kestrel-cli on a folder-format project. Each command is stateless
...                 (load, one transaction, save) and prints the id it created so that scripts can chain.
...                 The tests build one project step by step, so they run in order.
Resource            ../resources/kestrel.resource
Library             OperatingSystem

Suite Setup         Prepare Workspace


*** Variables ***
${WORK}     ${OUTPUT DIR}${/}project_cli_work


*** Test Cases ***
Create Project
    [Tags]    FR-PRJ-1
    ${result}=    Run Cli Json    project    new    ${DIR}    --name    Demo House
    Should Be True    ${result}[ok]
    File Should Exist    ${DIR}${/}kestrel.json
    File Should Exist    ${DIR}${/}project.json
    ${info}=    Run Cli Json    project    info    ${DIR}
    Should Be Equal    ${info}[name]    Demo House
    Should Be Equal    ${info}[gaStyle]    ThreeLevel

Creating A Project Twice Is Refused
    [Tags]    FR-PRJ-1
    ${result}=    Run Cli Json    project    new    ${DIR}    --name    Again    expected_rc=3
    Should Be Equal    ${result}[error][code]    Conflict

Opening A Missing Project Is A File Error
    [Tags]    FR-PRJ-1
    Run Cli Json    project    info    ${WORK}${/}does-not-exist    expected_rc=3

Build Topology
    [Documentation]    Area 1, line 1.1, device 1.1.1: the individual address is derived from the numbers.
    [Tags]    FR-TOP-1
    ${area}=    Run Cli For Id    topology    add-area    ${DIR}    1    Ground floor    prefix=area
    ${line}=    Run Cli For Id    topology    add-line    ${DIR}    ${area}    1    Main line    prefix=line
    ${device}=    Run Cli For Id    topology    add-device    ${DIR}    ${line}    Switch actuator    --number    1
    ...    prefix=dev
    Set Suite Variable    ${AREA}    ${area}
    Set Suite Variable    ${LINE}    ${line}
    Set Suite Variable    ${DEVICE}    ${device}
    ${info}=    Run Cli Json    project    info    ${DIR}
    Should Be Equal    ${info}[devices][0][id]    ${device}
    Should Be Equal    ${info}[devices][0][address]    1.1.1
    Should Be Equal    ${info}[lines][0][area]    ${area}

Topology Numbers Are Range Checked
    [Tags]    FR-TOP-1
    ${result}=    Run Cli Json    topology    add-area    ${DIR}    16    Too far    expected_rc=1
    Should Be Equal    ${result}[error][code]    InvalidArgument
    Run Cli Json    topology    add-line    ${DIR}    ${AREA}    one    Bad number    expected_rc=2
    Run Cli Json    topology    add-line    ${DIR}    not-an-id    1    Bad id    expected_rc=2

Duplicate Individual Address Is Rejected
    [Tags]    FR-TOP-2
    ${result}=    Run Cli Json    topology    add-device    ${DIR}    ${LINE}    Second actuator    --number    1
    ...    expected_rc=1
    Should Be Equal    ${result}[error][code]    Conflict
    ${info}=    Run Cli Json    project    info    ${DIR}
    Length Should Be    ${info}[devices]    1

Duplicate Individual Address In A Project File Is Reported
    [Documentation]    Hand-edited / imported data can still contain duplicates: validation reports TOP-001.
    [Tags]    FR-TOP-2
    Copy Directory    ${DATA}${/}projects${/}duplicate-ia    ${WORK}${/}duplicate-ia
    ${result}=    Run Cli Json    validate    ${WORK}${/}duplicate-ia    expected_rc=1
    ${codes}=    Diagnostic Codes    ${result}
    Should Be Equal    ${codes}[0]    TOP-001
    Should Be Equal As Integers    ${result}[errors]    1
    ${subjects}=    Set Variable    ${result}[diagnostics][0][subjects]
    Length Should Be    ${subjects}    2
    Should Contain    ${subjects}[0][text]    1.1.1

Add Group Addresses
    [Tags]    FR-GA-1
    ${light}=    Run Cli For Id    ga    add    ${DIR}    1/1/1    Light kitchen    --dpt    1.001    prefix=ga
    ${temperature}=    Run Cli For Id    ga    add    ${DIR}    1/1/2    Temperature kitchen    prefix=ga
    Set Suite Variable    ${GA_LIGHT}    ${light}
    Set Suite Variable    ${GA_TEMPERATURE}    ${temperature}
    ${info}=    Run Cli Json    project    info    ${DIR}
    Should Be Equal    ${info}[groupAddresses][0][address]    1/1/1
    Should Be Equal    ${info}[groupAddresses][0][dpt]    DPST-1-1
    Should Be Empty    ${info}[groupAddresses][1][dpt]

Duplicate Or Malformed Group Address Is Rejected
    [Tags]    FR-GA-1
    ${result}=    Run Cli Json    ga    add    ${DIR}    1/1/1    Duplicate    expected_rc=1
    Should Be Equal    ${result}[error][code]    Conflict
    Run Cli Json    ga    add    ${DIR}    32/0/0    Out of range    expected_rc=2
    Run Cli Json    ga    add    ${DIR}    1/1/5    Bad DPT    --dpt    banana    expected_rc=2

Link Com Object To Group Address
    [Documentation]    The first link of a com object becomes its sending link.
    [Tags]    FR-GA-3
    ${object}=    Run Cli For Id    co    add    ${DIR}    ${DEVICE}    0    Switch    --flags    CWT    --dpt    1.001
    ...    prefix=co
    Set Suite Variable    ${CO_SWITCH}    ${object}
    ${link}=    Run Cli For Id    link    ${DIR}    ${object}    ${GA_LIGHT}    prefix=lnk
    ${info}=    Run Cli Json    project    info    ${DIR}
    Should Be Equal    ${info}[comObjects][0][sizeInBit]    ${1}
    Should Be Equal    ${info}[links][0][id]    ${link}
    Should Be True    ${info}[links][0][sending]
    ${again}=    Run Cli Json    link    ${DIR}    ${object}    ${GA_LIGHT}    expected_rc=1
    Should Be Equal    ${again}[error][code]    Conflict

Validate Reports Warnings Without Failing
    [Documentation]    1/1/2 has no DPT (GA-002) and no links (GA-004); the device is not placed (BLD-001).
    [Tags]    FR-GA-4
    ${result}=    Run Cli Json    validate    ${DIR}
    ${codes}=    Diagnostic Codes    ${result}
    Should Contain    ${codes}    GA-002
    Should Contain    ${codes}    GA-004
    Should Contain    ${codes}    BLD-001
    Should Not Contain    ${codes}    GA-005
    Should Be Equal As Integers    ${result}[errors]    0
    ${text}=    Run Cli    validate    ${DIR}
    Should Contain    ${text}    GA-002
    Should Contain    ${text}    1/1/2

Validate Fails On A DPT Size Mismatch
    [Tags]    FR-GA-4
    ${object}=    Run Cli For Id    co    add    ${DIR}    ${DEVICE}    1    Temperature    --size    1    --flags    CWT
    ...    prefix=co
    Run Cli For Id    ga    add    ${DIR}    1/1/3    Temperature living    --dpt    9.001    prefix=ga
    ${info}=    Run Cli Json    project    info    ${DIR}
    ${ga}=    Set Variable    ${info}[groupAddresses][2][id]
    Run Cli For Id    link    ${DIR}    ${object}    ${ga}    prefix=lnk
    ${result}=    Run Cli Json    validate    ${DIR}    expected_rc=1
    ${codes}=    Diagnostic Codes    ${result}
    Should Be Equal    ${codes}[0]    GA-003
    Should Not Be True    ${result}[ok]

Placing The Device Clears The Building Finding
    [Tags]    FR-BLD-1    FR-BLD-2
    ${building}=    Run Cli For Id    building    add    ${DIR}    building    House    prefix=bp
    ${room}=    Run Cli For Id    building    add    ${DIR}    Room    Kitchen    --parent    ${building}    prefix=bp
    Run Cli For Id    topology    add-device    ${DIR}    ${LINE}    Presence detector    --number    2    --place
    ...    ${room}    prefix=dev
    ${result}=    Run Cli Json    validate    ${DIR}    expected_rc=1
    FOR    ${diagnostic}    IN    @{result}[diagnostics]
        IF    '${diagnostic}[code]' == 'BLD-001'
            Should Not Contain    ${diagnostic}[subjects][0][text]    Presence detector
        END
    END

Unknown Command Is A Usage Error
    ${result}=    Run Cli Json    topology    remove-everything    ${DIR}    expected_rc=2
    Should Not Be True    ${result}[ok]


*** Keywords ***
Prepare Workspace
    Remove Directory    ${WORK}    recursive=True
    Create Directory    ${WORK}
    Set Suite Variable    ${DIR}    ${WORK}${/}demo
