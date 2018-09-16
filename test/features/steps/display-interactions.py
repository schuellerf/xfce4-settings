import subprocess

# just a helper to fake better console output
ENTER = ""

def prompt(context, label):
	context.real_stdout.write("{}\n(Confirm: <ENTER>, Abort: !(OPTIONAL COMMENT)<ENTER> or Comment: COMMENT<ENTER>)\n".format(label))
	user_input = input()

	comment = user_input[1:] if user_input.startswith('!') else user_input
	if len(comment) > 0:
		print("USER COMMENT ({}): {}".format("Step: " + context.step.name, comment))

	assert user_input.startswith('!') is False

@given(u'no external monitors are attached')
def step_impl(context):
	prompt(context, u'Please disconnect all external monitors')

@given(u'all profiles are deleted')
def step_impl(context):
	prompt(context, u'I will start the display settings for you!')
	subprocess.Popen("xfce4-display-settings")
	prompt(context, u'Please remove all profiles')

@when(u'the {monitor} monitor is connected (via {connector}})')
def step_impl(context, monitor, connector):
	prompt(context, u'Please connect the {} monitor VIA {}'.format(monitor, connector))

@when(u'the {monitor} monitor is {state}')
def step_impl(context, monitor, state):
	prompt(context, u'Please assure that {} monitor is {}'.format(monitor, state))

@when(u'the display dialog is opened')
def step_impl(context):
	prompt(context, u'I will start the display settings for you!')
	subprocess.Popen("xfce4-display-settings")

@when(u'no profile exists')
def step_impl(context):
	prompt(context, u'Please check if there is no profile')

@when(u'the monitors get arranged: {arrangement}')
def step_impl(context, arrangement):
	prompt(context, u'Please arrange the monitors like this: {}'.format(arrangement))

@when(u'the configuration is applied')
def step_impl(context):
	prompt(context, u'Please apply the configuration')

@when(u'the new profile "{profile_name}" is saved')
def step_impl(context, profile_name):
	prompt(context, u'Please create a new profile: {}'.format(profile_name))

@when(u'"Configure new displays when connected" is set to enabled')
def step_impl(context):
	prompt(context, u'Please assure "Configure new displays when connected" to be enabled')

@when(u'the {dialog_type} dialog is closed')
@given(u'the {dialog_type} dialog is closed')
def step_impl(context, dialog_type):
	prompt(context, u'Please close the {} dialog'.format(dialog_type))

@then(u'the monitors are arranged: {arrangement}')
def step_impl(context, arrangement):
	prompt(context, u'Please verify the arrangement: {}'.format(arrangement))

