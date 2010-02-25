/*
 * Copyright 1995-2002 by Frederic Lepied, France. <Lepied@XFree86.org>
 * Copyright 2002-2009 by Ping Cheng, Wacom. <pingc@wacom.com>
 *                                                                            
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xf86Wacom.h"
#include "wcmFilter.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/*****************************************************************************
 * wcmAllocate --
 * Allocate the generic bits needed by any wacom device, regardless of type.
 ****************************************************************************/

static int wcmAllocate(LocalDevicePtr local)
{
	WacomDevicePtr   priv   = NULL;
	WacomCommonPtr   common = NULL;
	WacomToolPtr     tool   = NULL;
	WacomToolAreaPtr area   = NULL;
	int i;

	priv = xcalloc(1, sizeof(WacomDeviceRec));
	if (!priv)
		goto error;

	common = xcalloc(1, sizeof(WacomCommonRec));
	if (!common)
		goto error;

	tool = xcalloc(1, sizeof(WacomTool));
	if(!tool)
		goto error;

	area = xcalloc(1, sizeof(WacomToolArea));
	if (!area)
		goto error;

	local->flags = 0;
	local->device_control = gWacomModule.DevProc;
	local->read_input = gWacomModule.DevReadInput;
	local->control_proc = gWacomModule.DevChangeControl;
	local->close_proc = gWacomModule.DevClose;
	local->switch_mode = gWacomModule.DevSwitchMode;
	local->atom = 0;
	local->dev = NULL;
	local->private = priv;
	local->private_flags = 0;
	local->old_x = -1;
	local->old_y = -1;

	priv->next = NULL;
	priv->local = local;
	priv->common = common;       /* common info pointer */
	priv->hardProx = 1;	     /* previous hardware proximity */
	priv->screen_no = -1;        /* associated screen */
	priv->nPressCtrl [0] = 0;    /* pressure curve x0 */
	priv->nPressCtrl [1] = 0;    /* pressure curve y0 */
	priv->nPressCtrl [2] = 100;  /* pressure curve x1 */
	priv->nPressCtrl [3] = 100;  /* pressure curve y1 */

	/* Default button and expresskey values, offset buttons 4 and higher
	 * by the 4 scroll buttons. */
	for (i=0; i<WCM_MAX_BUTTONS; i++)
		priv->button[i] = (i < 3) ? i + 1 : i + 5;

	priv->nbuttons = WCM_MAX_BUTTONS;		/* Default number of buttons */
	priv->relup = 5;			/* Default relative wheel up event */
	priv->reldn = 4;			/* Default relative wheel down event */

	priv->wheelup = 4;			/* Default absolute wheel up event */
	priv->wheeldn = 5;			/* Default absolute wheel down event */
	priv->striplup = 4;			/* Default left strip up event */
	priv->stripldn = 5;			/* Default left strip down event */
	priv->striprup = 4;			/* Default right strip up event */
	priv->striprdn = 5;			/* Default right strip down event */
	priv->naxes = 6;			/* Default number of axes */
	priv->numScreen = screenInfo.numScreens; /* configured screens count */
	priv->currentScreen = -1;                /* current screen in display */
	priv->twinview = TV_NONE;		/* not using twinview gfx */
	priv->wcmMMonitor = 1;			/* enabled (=1) to support multi-monitor desktop. */
						/* disabled (=0) when user doesn't want to move the */
						/* cursor from one screen to another screen */

	/* JEJ - throttle sampling code */
	priv->throttleLimit = -1;

	common->wcmFlags = RAW_FILTERING_FLAG;   /* various flags */
	common->wcmDevices = priv;
	common->wcmProtocolLevel = 4;      /* protocol level */
	common->wcmISDV4Speed = 38400;  /* serial ISDV4 link speed */

	common->wcmDevCls = &gWacomUSBDevice; /* device-specific functions */
	common->wcmTPCButton = 
		common->wcmTPCButtonDefault; /* set Tablet PC button on/off */
	common->wcmCapacity = -1;          /* Capacity is disabled */
	common->wcmCapacityDefault = -1;    /* default to -1 when capacity isn't supported */
					   /* 3 when capacity is supported */
	common->wcmRotate = ROTATE_NONE;   /* default tablet rotation to off */
	common->wcmMaxX = 0;               /* max digitizer logical X value */
	common->wcmMaxY = 0;               /* max digitizer logical Y value */
	common->wcmMaxTouchX = 1024;       /* max touch X value */
	common->wcmMaxTouchY = 1024;       /* max touch Y value */
	common->wcmMaxStripX = 4096;       /* Max fingerstrip X */
	common->wcmMaxStripY = 4096;       /* Max fingerstrip Y */
	common->wcmMaxtiltX = 128;	   /* Max tilt in X directory */
	common->wcmMaxtiltY = 128;	   /* Max tilt in Y directory */
	common->wcmCursorProxoutDistDefault = PROXOUT_INTUOS_DISTANCE; 
			/* default to Intuos */
	common->wcmSuppress = DEFAULT_SUPPRESS;    
			/* transmit position if increment is superior */
	common->wcmRawSample = DEFAULT_SAMPLES;    
			/* number of raw data to be used to for filtering */

	/* tool */
	priv->tool = tool;
	common->wcmTool = tool;
	tool->next = NULL;          /* next tool in list */
	tool->arealist = area;      /* list of defined areas */
	/* tool->typeid is set once we know the type - see wcmSetType */

	/* tool area */
	priv->toolarea = area;
	area->next = NULL;    /* next area in list */
	area->device = local; /* associated WacomDevice */

	return 1;

error:
	xfree(area);
	xfree(tool);
	xfree(common);
	xfree(priv);
	return 0;
}

static int wcmSetType(LocalDevicePtr local, const char *type, int tablet_id)
{
	WacomDevicePtr priv = local->private;

	if (!type)
	{
		xf86Msg(X_ERROR, "%s: No type or invalid type specified.\n"
				"Must be one of stylus, touch, cursor, eraser, or pad\n",
				local->name);
		return 0;
	}

	if (xf86NameCmp(type, "stylus") == 0)
	{
		priv->flags = ABSOLUTE_FLAG|STYLUS_ID;
		local->type_name = XI_STYLUS;
	} else if (xf86NameCmp(type, "touch") == 0)
	{
		int flags = TOUCH_ID;

		if (tablet_id < 0xd0 || tablet_id > 0xd3)
			flags |= ABSOLUTE_FLAG;

		priv->flags = flags;
		local->type_name = XI_TOUCH;
	} else if (xf86NameCmp(type, "cursor") == 0)
	{
		priv->flags = CURSOR_ID;
		local->type_name = XI_CURSOR;
	} else if (xf86NameCmp(type, "eraser") == 0)
	{
		priv->flags = ABSOLUTE_FLAG|ERASER_ID;
		local->type_name = XI_ERASER;
	} else if (xf86NameCmp(type, "pad") == 0)
	{
		priv->flags = PAD_ID;
		local->type_name = XI_PAD;
	}

	/* Set the device id of the "last seen" device on this tool */
	if (IsStylus(priv))
		priv->old_device_id = STYLUS_DEVICE_ID;
	else if (IsEraser(priv))
		priv->old_device_id = ERASER_DEVICE_ID;
	else if (IsCursor(priv))
		priv->old_device_id = CURSOR_DEVICE_ID;
	else if (IsTouch(priv))
		priv->old_device_id = TOUCH_DEVICE_ID;
	else
		priv->old_device_id = PAD_DEVICE_ID;

	if (!priv->tool)
		return 0;

	priv->tool->typeid = DEVICE_ID(priv->flags); /* tool type (stylus/touch/eraser/cursor/pad) */

	return 1;
}

/* 
 * Be sure to set vmin appropriately for your device's protocol. You want to
 * read a full packet before returning
 *
 * JEJ - Actually, anything other than 1 is probably a bad idea since packet
 * errors can occur.  When that happens, bytes are read individually until it
 * starts making sense again.
 */

static const char *default_options[] =
{
	"StopBits",    "1",
	"DataBits",    "8",
	"Parity",      "None",
	"Vmin",        "1",
	"Vtime",       "10",
	"FlowControl", "Xoff",
	NULL
};

/* wcmUninit - called when the device is no longer needed. */

static void wcmUninit(InputDriverPtr drv, LocalDevicePtr local, int flags)
{
	WacomDevicePtr priv = (WacomDevicePtr) local->private;
	WacomDevicePtr dev;
	WacomDevicePtr *prev;

	DBG(1, priv, "\n");

	if (priv->isParent)
	{
		/* HAL removal sees the parent device removed first. */
		WacomDevicePtr next;
		dev = priv->common->wcmDevices;

		xf86Msg(X_INFO, "%s: removing automatically added devices.\n",
			local->name);

		while(dev)
		{
			next = dev->next;
			if (!dev->isParent)
			{
				xf86Msg(X_INFO, "%s: removing dependent device '%s'\n",
					local->name, dev->local->name);
				DeleteInputDeviceRequest(dev->local->dev);
			}
			dev = next;
		}
	}

	prev = &priv->common->wcmDevices;
	dev = *prev;
	while(dev)
	{
		if (dev == priv)
		{
			*prev = dev->next;
			break;
		}
		prev = &dev->next;
		dev = dev->next;
	}

	/* free pressure curve */
	xfree(priv->pPressCurve);

	xfree(priv);
	local->private = NULL;


	xf86DeleteInput(local, 0);    
}

/* wcmMatchDevice - locate matching device and merge common structure. If an
 * already initialized device shares the same device file and driver, remove
 * the new device's "common" struct and point to the one of the already
 * existing one instead.
 * Then add the new device to the now-shared common struct.
 */
static Bool wcmMatchDevice(LocalDevicePtr pLocal)
{
	WacomDevicePtr priv = (WacomDevicePtr)pLocal->private;
	WacomCommonPtr common = priv->common;
	LocalDevicePtr pMatch = xf86FirstLocalDevice();

	if (!common->wcmDevice)
		return 0;

	for (; pMatch != NULL; pMatch = pMatch->next)
	{
		WacomDevicePtr privMatch = (WacomDevicePtr)pMatch->private;

		if ((pLocal != pMatch) &&
				strstr(pMatch->drv->driverName, "wacom") &&
				!strcmp(privMatch->common->wcmDevice, common->wcmDevice))
		{
			DBG(2, priv, "port share between"
					" %s and %s\n", pLocal->name, pMatch->name);
			xfree(common);
			common = priv->common = privMatch->common;
			priv->next = common->wcmDevices;
			common->wcmDevices = priv;
			return 1;
		}
	}
	return 0;
}

/* wcmPreInit - called for each input devices with the driver set to
 * "wacom" */
static LocalDevicePtr wcmPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
	LocalDevicePtr local = NULL;
	WacomDevicePtr priv = NULL;
	WacomCommonPtr common = NULL;
	const char*	type;
	char*		device;
	int		need_hotplug = 0;
	unsigned long   keys[NBITS(KEY_MAX)];
	int		tablet_id = 0;

	gWacomModule.wcmDrv = drv;

	if (!(local = xf86AllocateInput(drv, 0)))
		goto SetupProc_fail;

	local->conf_idev = dev;
	local->name = dev->identifier;

	/* Force default port options to exist because the init
	 * phase is based on those values.
	 */
	xf86CollectInputOptions(local, default_options, NULL);

	device = xf86SetStrOption(local->options, "Device", NULL);
	type = xf86FindOptionValue(local->options, "Type");

	/*
	   Init process:
	   - allocate the generic struct used by all device types.
	   - if no device is given, auto-probe for one (find a wacom device
	     in /dev/input/event?
	   - open the device file
	   - probe the device
	   - remove duplicate devices if needed
	   - set the device type
	   - hotplug dependent devices if needed
	 */

	if (!wcmAllocate(local))
		goto SetupProc_fail;

	if (!device)
	{
		if (!wcmAutoProbeDevice(local))
			goto SetupProc_fail;

		device = xf86SetStrOption(local->options, "Device", NULL);
	}

	SYSCALL(local->fd = open(device, O_RDONLY));
	if (local->fd < 0)
	{
		xf86Msg(X_WARNING, "%s: failed to open %s.\n",
				local->name, device);
		goto SetupProc_fail;
	}

	/* initialize supported keys */
	wcmDeviceTypeKeys(local, keys, &tablet_id);
	need_hotplug = wcmNeedAutoHotplug(local, &type, keys);

	/* check if the type is valid for those don't need hotplug */
	if(!need_hotplug && !wcmIsAValidType(type, keys))
		goto SetupProc_fail;

	/* check if the same device file has been added already */
	if (wcmIsDuplicate(device, local))
		goto SetupProc_fail;

	if (!wcmSetType(local, type, tablet_id))
		goto SetupProc_fail;

	priv = (WacomDevicePtr) local->private;
	priv->name = local->name;
	common = priv->common;

	common->wcmDevice = device;

	/* Hardware specific initialization relies on tablet_id */
	common->tablet_id = tablet_id;

	/* Lookup to see if there is another wacom device sharing the same port */
	if (wcmMatchDevice(local))
		common = priv->common;

	/* Process the common options. */
	xf86ProcessCommonOptions(local, local->options);
	if (!wcmParseOptions(local, keys))
		goto SetupProc_fail;

	/* mark the device configured */
	local->flags |= XI86_POINTER_CAPABLE | XI86_CONFIGURED;

	if (need_hotplug)
	{
		priv->isParent = 1;
		wcmHotplugOthers(local, keys);
	}

	if (local->fd != -1)
	{
		close(local->fd);
		local->fd = -1;
	}

	return (local);

SetupProc_fail:
	xfree(common);
	xfree(priv);
	if (local)
	{
		if (local->fd != -1)
		{
			close(local->fd);
			local->fd = -1;
		}

		local->private = NULL;
		xf86DeleteInput(local, 0);
	}

	return NULL;
}

InputDriverRec WACOM =
{
	1,             /* driver version */
	"wacom",       /* driver name */
	NULL,          /* identify */
	wcmPreInit,    /* pre-init */
	wcmUninit, /* un-init */
	NULL,          /* module */
	0              /* ref count */
};


/* wcmUnplug - Uninitialize the device */

static void wcmUnplug(pointer p)
{
}

/* wcmPlug - called by the module loader */

static pointer wcmPlug(pointer module, pointer options, int* errmaj,
		int* errmin)
{
	xf86AddInputDriver(&WACOM, module, 0);
	return module;
}

static XF86ModuleVersionInfo wcmVersionRec =
{
	"wacom",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}  /* signature, to be patched into the file by a tool */
};

_X_EXPORT XF86ModuleData wacomModuleData =
{
	&wcmVersionRec,
	wcmPlug,
	wcmUnplug
};

/* vim: set noexpandtab shiftwidth=8: */
