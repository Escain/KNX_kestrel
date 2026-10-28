*** Settings ***
Documentation       Shared keywords for the kestrel-cli / kestrel-sim acceptance suites.
...                 ctest passes ${CLI}, ${SIM} and ${DATA}; the defaults below allow running a suite
...                 by hand from the repository root against the debug build.
Library             Collections
Library             KestrelCli
Library             KestrelSim


*** Variables ***
${CLI}      build/debug/apps/kestrel-cli/kestrel-cli
${SIM}      build/debug/apps/kestrel-sim/kestrel-sim
${DATA}     tests/data


*** Keywords ***
Diagnostic Codes
    [Documentation]    Codes of a ``validate --json`` result, in report order.
    [Arguments]    ${result}
    ${codes}=    Create List
    FOR    ${diagnostic}    IN    @{result}[diagnostics]
        Append To List    ${codes}    ${diagnostic}[code]
    END
    RETURN    ${codes}

Telegram Events
    [Documentation]    Keeps the ``{"event":"telegram"}`` objects of a monitor output.
    [Arguments]    ${lines}
    ${telegrams}=    Create List
    FOR    ${line}    IN    @{lines}
        IF    $line.get('event') == 'telegram'
            Append To List    ${telegrams}    ${line}
        END
    END
    RETURN    ${telegrams}
