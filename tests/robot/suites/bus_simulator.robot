*** Settings ***
Documentation       Bus use cases of kestrel-cli against a kestrel-sim process over loopback UDP (NFR-5:
...                 every bus operation is testable without hardware). Scenario: tests/data/sim/basic.json
...                 (switch-a at 1.1.1 with 1/1/1 switch CWTU, 1/1/2 temperature CRT = 21.5 °C, 1/1/3 dimming
...                 CWU; "fresh" at 15.15.255 and "spare" at 15.15.254, both factory-new).
Resource            ../resources/kestrel.resource

Suite Setup         Start Bus Simulator
Suite Teardown      Stop Simulator
Test Tags           NFR-5


*** Variables ***
${SCENARIO}     ${DATA}/sim/basic.json


*** Test Cases ***
Group Write Changes Simulated Device Value
    [Documentation]    A_GroupValue_Write through the tunnel reaches the device's group object.
    [Tags]    FR-BUS-6    FR-BUS-2
    ${result}=    Run Cli Json    group    write    1/1/1    1.001    on    --interface    ${IF}
    Should Be Equal    ${result}[raw]    01
    Simulated Group Value Should Be    switch-a    1/1/1    01
    Run Cli Json    group    write    1/1/3    5.001    100    --interface    ${IF}
    Simulated Group Value Should Be    switch-a    1/1/3    ff
    Run Cli Json    group    write    1/1/1    1.001    off    --interface    ${IF}
    Simulated Group Value Should Be    switch-a    1/1/1    00

Group Write Rejects Invalid Value
    [Tags]    FR-BUS-6
    ${result}=    Run Cli Json    group    write    1/1/3    5.001    150    --interface    ${IF}
    ...    expected_rc=2
    Should Not Be True    ${result}[ok]

Group Read Returns Decoded Value
    [Documentation]    The device answers A_GroupValue_Read on an object with the R flag.
    [Tags]    FR-BUS-6
    ${result}=    Run Cli Json    group    read    1/1/2    9.001    --interface    ${IF}
    Should Be Equal As Numbers    ${result}[value]    21.5
    Should Be Equal    ${result}[source]    1.1.1
    Should Be Equal    ${result}[raw]    0c 33

Group Read Without Read Flag Times Out
    [Tags]    FR-BUS-6
    ${result}=    Run Cli Json    group    read    1/1/1    1.001    --interface    ${IF}    --timeout    0.5
    ...    expected_rc=4
    Should Be Equal    ${result}[error][code]    Timeout

Program Individual Address Via Programming Button
    [Documentation]    program-ia waits for a device in programming mode, checks the address is free,
    ...    writes it, verifies and restarts the device (which leaves programming mode).
    [Tags]    FR-COM-1
    Wait Until Simulator Has No Clients
    ${cli}=    Start Cli    device    program-ia    1.1.20    --interface    ${IF}    --timeout    30
    Wait Until Simulator Has Clients    1
    Press Programming Button    fresh
    ${output}=    Wait For Cli    ${cli}    timeout=60
    Should Be Equal    ${output}[-1][address]    1.1.20
    Should Be Equal    ${output}[-1][previousAddress]    15.15.255
    ${device}=    Get Simulated Device    fresh
    Should Be Equal    ${device}[address]    1.1.20
    Should Not Be True    ${device}[progMode]
    Should Be Equal As Integers    ${device}[restartCount]    1

Program Individual Address Refuses An Address In Use
    [Tags]    FR-COM-1
    Press Programming Button    spare
    ${result}=    Run Cli Json    device    program-ia    1.1.1    --interface    ${IF}    --timeout    10
    ...    expected_rc=4    timeout=60
    Should Be Equal    ${result}[error][code]    Conflict
    ${device}=    Get Simulated Device    spare
    Should Be Equal    ${device}[address]    15.15.254
    Press Programming Button    spare

Program Individual Address Times Out Without Device In Programming Mode
    [Tags]    FR-COM-1
    ${result}=    Run Cli Json    device    program-ia    1.1.30    --interface    ${IF}    --timeout    1.5
    ...    expected_rc=4    timeout=30
    Should Be Equal    ${result}[error][code]    Timeout

Device Info Reads Descriptor Serial And Manufacturer
    [Tags]    FR-COM-4
    ${info}=    Run Cli Json    device    info    1.1.1    --interface    ${IF}
    Should Be Equal    ${info}[address]    1.1.1
    Should Be Equal    ${info}[maskVersion]    0705
    Should Be Equal    ${info}[serial]    ffff:00001234
    Should Be Equal    ${info}[manufacturer]    M-FFFF

Monitor Sees Bus Telegrams
    [Documentation]    The device application sends two values; the monitor prints both, decoded.
    [Tags]    FR-BUS-5
    Wait Until Simulator Has No Clients
    ${cli}=    Start Cli    monitor    --interface    ${IF}    --count    2    --timeout    20
    ...    --dpt    1/1/1\=1.001
    Wait Until Simulator Has Clients    1
    Set Simulated Group Value    switch-a    1/1/1    01
    Set Simulated Group Value    switch-a    1/1/1    00
    ${lines}=    Wait For Cli    ${cli}    timeout=30
    ${telegrams}=    Telegram Events    ${lines}
    Length Should Be    ${telegrams}    2
    Should Be Equal    ${telegrams}[0][source]    1.1.1
    Should Be Equal    ${telegrams}[0][destination]    1/1/1
    Should Be Equal    ${telegrams}[0][service]    GroupValueWrite
    Should Be Equal    ${telegrams}[0][data]    01
    Should Be Equal    ${telegrams}[1][data]    00
    Should Not Be Equal    ${telegrams}[0][value]    ${telegrams}[1][value]

Discovery Finds The Simulated Interface
    [Tags]    FR-BUS-1
    ${result}=    Run Cli Json    bus    discover    --host    127.0.0.1    --port    ${PORT}    --timeout    1
    Length Should Be    ${result}[interfaces]    1
    ${found}=    Set Variable    ${result}[interfaces][0]
    Should Be Equal As Integers    ${found}[port]    ${PORT}
    Should Be Equal    ${found}[host]    127.0.0.1
    Should Be Equal    ${found}[name]    Kestrel simulator
    Should Be Equal    ${found}[address]    1.1.255
    Should Be True    ${found}[tunnelling]

Unreachable Interface Is A Bus Error
    [Documentation]    Nobody listens on the port: the tunnel connect times out (10 s) and exits with 4.
    [Tags]    FR-BUS-2
    ${result}=    Run Cli Json    group    write    1/1/1    1.001    on    --interface    127.0.0.1:9
    ...    expected_rc=4    timeout=30
    Should Not Be True    ${result}[ok]

Telegrams Are Visible In The Simulator Log
    [Documentation]    Every telegram on the simulated medium is reported by kestrel-sim.
    [Tags]    FR-BUS-5
    ${before}=    Get Simulator Telegram Count
    Run Cli Json    group    write    1/1/1    1.001    on    --interface    ${IF}
    ${telegrams}=    Get Simulator Telegrams
    ${after}=    Get Length    ${telegrams}
    Should Be True    ${after} > ${before}
    ${last}=    Set Variable    ${telegrams}[-1]
    Should Be Equal    ${last}[destination]    1/1/1
    Should Be Equal    ${last}[apci]    GroupValueWrite
    Should Be Equal    ${last}[source]    1.1.250


*** Keywords ***
Start Bus Simulator
    ${port}=    Start Simulator    ${SCENARIO}
    Set Suite Variable    ${PORT}    ${port}
    Set Suite Variable    ${IF}    127.0.0.1:${port}
