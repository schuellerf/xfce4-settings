Feature: Display Profiles
    Checks the functionality of the display Profiles
    NOTE: This test includes manual steps!
    This test also assumes that you have two external monitors
    in addition to your laptop monitor
    The monitors called "LAPTOP", "FIRST" and "SECOND" later on
    The connectors will be called CONNECTOR_1 and CONNECTOR_2 (for the two external monitors)

    @onemonitor
    @twomonitors
    Scenario: One external monitor "extend to the left"
        Given no external monitors are attached
          and all profiles are deleted
          and the display dialog is closed
         When the FIRST monitor is connected
          and the minimal dialog is closed
          and the display dialog is opened
          and no profile exists
          and the monitors get arranged: FIRST left of LAPTOP (top aligned)
          and the configuration is applied
          and the new profile "Left extend test" is saved
          and "Configure new displays when connected" is set to enabled
          and the display dialog is closed
          and the FIRST monitor is disconnected
          and the FIRST monitor is connected
         Then the monitors are arranged: FIRST left of LAPTOP (top aligned)

    @twomonitors
    Scenario: Switch connectors
        Given no external monitors are attached
          and all profiles are deleted
          and the display dialog is closed
         When the FIRST monitor is connected (via CONNECTOR_1)
          and the SECOND monitor is connected (via CONNECTOR_2)
          and the display dialog is opened
          and no profile exists
          and the monitors get arranged: FIRST left of LAPTOP (top aligned), SECOND left of FIRST (top aligned)
          and the configuration is applied
          and the new profile "All to the left" is saved
          and "Configure new displays when connected" is set to enabled
          and the display dialog is closed
          and the FIRST monitor is disconnected
          and the SECOND monitor is disconnected
          and the FIRST monitor is connected (via CONNECTOR_2)
          and the SECOND monitor is connected (via CONNECTOR_1)
         Then the monitors are arranged: FIRST left of LAPTOP (top aligned), SECOND left of FIRST (top aligned)
