/*-
 * Copyright (c) 2000 Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/types.h>

#include <machine/stdarg.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_enc.h>
#include <cam/scsi/scsi_enc_internal.h>

#include <opt_enc.h>

MALLOC_DEFINE(M_SCSIENC, "SCSI ENC", "SCSI ENC buffers");

/* Enclosure type independent driver */

#define	SEN_ID		"UNISYS           SUN_SEN"
#define	SEN_ID_LEN	24

static	d_open_t	enc_open;
static	d_close_t	enc_close;
static	d_ioctl_t	enc_ioctl;
static	periph_init_t	enc_init;
static  periph_ctor_t	enc_ctor;
static	periph_oninv_t	enc_oninvalidate;
static  periph_dtor_t   enc_dtor;
static  periph_start_t  enc_start;

static void enc_async(void *, uint32_t, struct cam_path *, void *);
static enctyp enc_type(void *, int);

static struct periph_driver encdriver = {
	enc_init, "enc",
	TAILQ_HEAD_INITIALIZER(encdriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(enc, encdriver);

static struct cdevsw enc_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	enc_open,
	.d_close =	enc_close,
	.d_ioctl =	enc_ioctl,
	.d_name =	"enc",
	.d_flags =	0,
};

static void
enc_init(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, enc_async, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("enc: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

static void
enc_oninvalidate(struct cam_periph *periph)
{
	struct enc_softc *enc;

	enc = periph->softc;

	/* If the sub-driver has an invalidate routine, call it */
	if (enc->enc_vec.softc_invalidate != NULL)
		enc->enc_vec.softc_invalidate(periph);

	/*
	 * Unregister any async callbacks.
	 */
	xpt_register_async(0, enc_async, periph, periph->path);

	/*
	 * Shutdown our daemon.
	 */
	enc->enc_flags |= ENC_FLAG_SHUTDOWN;
	if (enc->enc_daemon != NULL) {
		/* Signal and wait for the ses daemon to terminate. */
		wakeup(enc->enc_daemon);
		/*
		 * We're called with the SIM mutex held, but we're dropping
		 * the update mutex here on sleep.  So we have to manually
		 * drop the SIM mutex.
		 */
		cam_periph_sleep(enc->periph, enc->enc_daemon,
				 PUSER, "thtrm", 0);
	}
	callout_drain(&enc->status_updater);

	enc->enc_flags |= ENC_FLAG_INVALID;

	xpt_print(periph->path, "lost device\n");
}

static void
enc_dtor(struct cam_periph *periph)
{
	struct enc_softc *enc;

	enc = periph->softc;

	xpt_print(periph->path, "removing device entry\n");
	cam_periph_unlock(periph);
	destroy_dev(enc->enc_dev);
	cam_periph_lock(periph);

	/* If the sub-driver has a cleanup routine, call it */
	if (enc->enc_vec.softc_cleanup != NULL)
		enc->enc_vec.softc_cleanup(periph);

	if (enc->enc_boot_hold_ch.ich_func != NULL) {
		config_intrhook_disestablish(&enc->enc_boot_hold_ch);
		enc->enc_boot_hold_ch.ich_func = NULL;
	}

	ENC_FREE(enc);
}

static void
enc_async(void *callback_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)callback_arg;

	switch(code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;
		int inq_len;
		path_id_t path_id;

		cgd = (struct ccb_getdev *)arg;
		if (arg == NULL) {
			break;
		}

		if (cgd->protocol != PROTO_SCSI)
			break;

		inq_len = cgd->inq_data.additional_length + 4;

		/*
		 * PROBLEM: WE NEED TO LOOK AT BYTES 48-53 TO SEE IF THIS
		 * PROBLEM: IS A SAF-TE DEVICE.
		 */
		switch (enc_type(&cgd->inq_data, inq_len)) {
		case ENC_SES:
		case ENC_SES_SCSI2:
		case ENC_SES_PASSTHROUGH:
		case ENC_SEN:
		case ENC_SAFT:
			break;
		default:
			/*
			 * Schedule announcement of the ENC bindings for
			 * this device if it is managed by a SEP.
			 */
			path_id = xpt_path_path_id(path);
			xpt_lock_buses();
			TAILQ_FOREACH(periph, &encdriver.units, unit_links) {
				struct enc_softc *softc;

				softc = (struct enc_softc *)periph->softc;
				if (xpt_path_path_id(periph->path) != path_id
				 || softc == NULL
				 || (softc->enc_flags & ENC_FLAG_INITIALIZED)
				  == 0
				 || softc->enc_vec.device_found == NULL)
					continue;

				softc->enc_vec.device_found(softc);
			}
			xpt_unlock_buses();
			return;
		}

		status = cam_periph_alloc(enc_ctor, enc_oninvalidate,
		    enc_dtor, enc_start, "enc", CAM_PERIPH_BIO,
		    cgd->ccb_h.path, enc_async, AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP && status != CAM_REQ_INPROG) {
			printf("enc_async: Unable to probe new device due to "
			    "status 0x%x\n", status);
		}
		break;
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static int
enc_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct enc_softc *softc;
	int error = 0;

	periph = (struct cam_periph *)dev->si_drv1;
	if (periph == NULL) {
		return (ENXIO);
	}

	if (cam_periph_acquire(periph) != CAM_REQ_CMP) {
		cam_periph_unlock(periph);
		return (ENXIO);
	}

	cam_periph_lock(periph);

	softc = (struct enc_softc *)periph->softc;

	if ((softc->enc_flags & ENC_FLAG_INITIALIZED) == 0) {
		error = ENXIO;
		goto out;
	}
	if (softc->enc_flags & ENC_FLAG_INVALID) {
		error = ENXIO;
		goto out;
	}

out:
	cam_periph_unlock(periph);
	if (error) {
		cam_periph_release(periph);
	}
	return (error);
}

static int
enc_close(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct enc_softc *softc;
	int error;

	error = 0;

	periph = (struct cam_periph *)dev->si_drv1;
	if (periph == NULL)
		return (ENXIO);

	cam_periph_lock(periph);

	softc = (struct enc_softc *)periph->softc;

	cam_periph_unlock(periph);
	cam_periph_release(periph);

	return (0);
}

static void
enc_start(struct cam_periph *p, union ccb *sccb)
{
	struct enc_softc *enc;

	enc = p->softc;
	ENC_DLOG(enc, "%s enter imm=%d prio=%d\n",
	    __func__, p->immediate_priority, p->pinfo.priority);
	if (p->immediate_priority <= p->pinfo.priority) {
		SLIST_INSERT_HEAD(&p->ccb_list, &sccb->ccb_h, periph_links.sle);
		p->immediate_priority = CAM_PRIORITY_NONE;
		wakeup(&p->ccb_list);
	} else
		xpt_release_ccb(sccb);
	ENC_DLOG(enc, "%s exit\n", __func__);
}

void
enc_done(struct cam_periph *periph, union ccb *dccb)
{
	wakeup(&dccb->ccb_h.cbfcnp);
}

int
enc_error(union ccb *ccb, uint32_t cflags, uint32_t sflags)
{
	struct enc_softc *softc;
	struct cam_periph *periph;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct enc_softc *)periph->softc;

	return (cam_periph_error(ccb, cflags, sflags, &softc->saved_ccb));
}

static int
enc_ioctl(struct cdev *dev, u_long cmd, caddr_t arg_addr, int flag,
	 struct thread *td)
{
	struct cam_periph *periph;
	encioc_enc_status_t tmp;
	encioc_string_t sstr;
	encioc_elm_status_t elms;
	encioc_elm_desc_t elmd;
	encioc_elm_devnames_t elmdn;
	encioc_element_t *uelm;
	enc_softc_t *enc;
	enc_cache_t *cache;
	void *addr;
	int error, i;


	if (arg_addr)
		addr = *((caddr_t *) arg_addr);
	else
		addr = NULL;

	periph = (struct cam_periph *)dev->si_drv1;
	if (periph == NULL)
		return (ENXIO);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering encioctl\n"));

	cam_periph_lock(periph);
	enc = (struct enc_softc *)periph->softc;
	cache = &enc->enc_cache;

	/*
	 * Now check to see whether we're initialized or not.
	 * This actually should never fail as we're not supposed
	 * to get past enc_open w/o successfully initializing
	 * things.
	 */
	if ((enc->enc_flags & ENC_FLAG_INITIALIZED) == 0) {
		cam_periph_unlock(periph);
		return (ENXIO);
	}
	cam_periph_unlock(periph);

	error = 0;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
	    ("trying to do ioctl %#lx\n", cmd));

	/*
	 * If this command can change the device's state,
	 * we must have the device open for writing.
	 *
	 * For commands that get information about the
	 * device- we don't need to lock the peripheral
	 * if we aren't running a command.  The periph
	 * also can't go away while a user process has
	 * it open.
	 */
	switch (cmd) {
	case ENCIOC_GETNELM:
	case ENCIOC_GETELMMAP:
	case ENCIOC_GETENCSTAT:
	case ENCIOC_GETELMSTAT:
	case ENCIOC_GETELMDESC:
	case ENCIOC_GETELMDEVNAMES:
		break;
	default:
		if ((flag & FWRITE) == 0) {
			return (EBADF);
		}
	}
 
	/*
	 * XXX The values read here are only valid for the current
	 *     configuration generation.  We need these ioctls
	 *     to also pass in/out a generation number.
	 */
	sx_slock(&enc->enc_cache_lock);
	switch (cmd) {
	case ENCIOC_GETNELM:
		error = copyout(&cache->nelms, addr, sizeof (cache->nelms));
		break;
		
	case ENCIOC_GETELMMAP:
		for (uelm = addr, i = 0; i != cache->nelms; i++) {
			encioc_element_t kelm;
			kelm.elm_idx = i;
			kelm.elm_subenc_id = cache->elm_map[i].subenclosure;
			kelm.elm_type = cache->elm_map[i].enctype;
			error = copyout(&kelm, &uelm[i], sizeof(kelm));
			if (error)
				break;
		}
		break;

	case ENCIOC_GETENCSTAT:
		cam_periph_lock(periph);
		error = enc->enc_vec.get_enc_status(enc, 1);
		if (error) {
			cam_periph_unlock(periph);
			break;
		}
		tmp = cache->enc_status & ~ENCI_SVALID;
		cam_periph_unlock(periph);
		error = copyout(&tmp, addr, sizeof(tmp));
		cache->enc_status = tmp;
		break;

	case ENCIOC_SETENCSTAT:
		error = copyin(addr, &tmp, sizeof(tmp));
		if (error)
			break;
		cam_periph_lock(periph);
		error = enc->enc_vec.set_enc_status(enc, tmp, 1);
		cam_periph_unlock(periph);
		break;

	case ENCIOC_GETSTRING:
	case ENCIOC_SETSTRING:
		if (enc->enc_vec.handle_string == NULL) {
			error = EINVAL;
			break;
		}
		error = copyin(addr, &sstr, sizeof(sstr));
		if (error)
			break;
		cam_periph_lock(periph);
		error = enc->enc_vec.handle_string(enc, &sstr, cmd);
		cam_periph_unlock(periph);
		break;

	case ENCIOC_GETELMSTAT:
		error = copyin(addr, &elms, sizeof(elms));
		if (error)
			break;
		if (elms.elm_idx >= cache->nelms) {
			error = EINVAL;
			break;
		}
		cam_periph_lock(periph);
		error = enc->enc_vec.get_elm_status(enc, &elms, 1);
		cam_periph_unlock(periph);
		if (error)
			break;
		error = copyout(&elms, addr, sizeof(elms));
		break;

	case ENCIOC_GETELMDESC:
		if (enc->enc_vec.get_elm_desc == NULL) {
			error = EINVAL;
			break;
		}
		error = copyin(addr, &elmd, sizeof(elmd));
		if (error)
			break;
		if (elmd.elm_idx >= cache->nelms) {
			error = EINVAL;
			break;
		}
		error = enc->enc_vec.get_elm_desc(enc, &elmd);
		if (error)
			break;
		error = copyout(&elmd, addr, sizeof(elmd));
		break;

	case ENCIOC_GETELMDEVNAMES:
		if (enc->enc_vec.get_elm_devnames == NULL) {
			error = EINVAL;
			break;
		}
		error = copyin(addr, &elmdn, sizeof(elmdn));
		if (error)
			break;
		if (elmdn.elm_idx >= cache->nelms) {
			error = EINVAL;
			break;
		}
		cam_periph_lock(periph);
		error = (*enc->enc_vec.get_elm_devnames)(enc, &elmdn);
		cam_periph_unlock(periph);
		if (error)
			break;
		error = copyout(&elmdn, addr, sizeof(elmdn));
		break;

	case ENCIOC_SETELMSTAT:
		error = copyin(addr, &elms, sizeof(elms));
		if (error)
			break;

		if (elms.elm_idx >= cache->nelms) {
			error = EINVAL;
			break;
		}
		cam_periph_lock(periph);
		error = enc->enc_vec.set_elm_status(enc, &elms, 1);
		cam_periph_unlock(periph);

		break;

	case ENCIOC_INIT:

		cam_periph_lock(periph);
		error = enc->enc_vec.init_enc(enc);
		cam_periph_unlock(periph);
		break;

	default:
		cam_periph_lock(periph);
		error = cam_periph_ioctl(periph, cmd, arg_addr, enc_error);
		cam_periph_unlock(periph);
		break;
	}
	sx_sunlock(&enc->enc_cache_lock);
	return (error);
}

int
enc_runcmd(struct enc_softc *enc, char *cdb, int cdbl, char *dptr, int *dlenp)
{
	int error, dlen;
	ccb_flags ddf;
	union ccb *ccb;

	CAM_DEBUG(enc->periph->path, CAM_DEBUG_TRACE,
	    ("entering enc_runcmd\n"));
	if (dptr) {
		if ((dlen = *dlenp) < 0) {
			dlen = -dlen;
			ddf = CAM_DIR_OUT;
		} else {
			ddf = CAM_DIR_IN;
		}
	} else {
		dlen = 0;
		ddf = CAM_DIR_NONE;
	}

	if (cdbl > IOCDBLEN) {
		cdbl = IOCDBLEN;
	}

	ccb = cam_periph_getccb(enc->periph, 1);
	cam_fill_csio(&ccb->csio, 0, enc_done, ddf, MSG_SIMPLE_Q_TAG, dptr,
	    dlen, sizeof (struct scsi_sense_data), cdbl, 60 * 1000);
	bcopy(cdb, ccb->csio.cdb_io.cdb_bytes, cdbl);

	error = cam_periph_runccb(ccb, enc_error, ENC_CFLAGS, ENC_FLAGS, NULL);
	if ((ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
		cam_release_devq(ccb->ccb_h.path, 0, 0, 0, FALSE);
	if (error) {
		if (dptr) {
			*dlenp = dlen;
		}
	} else {
		if (dptr) {
			*dlenp = ccb->csio.resid;
		}
	}
	xpt_release_ccb(ccb);
	CAM_DEBUG(enc->periph->path, CAM_DEBUG_SUBTRACE,
	    ("exiting enc_runcmd: *dlenp = %d\n", *dlenp));
	return (error);
}

void
enc_log(struct enc_softc *enc, const char *fmt, ...)
{
	va_list ap;

	printf("%s%d: ", enc->periph->periph_name, enc->periph->unit_number);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

/*
 * The code after this point runs on many platforms,
 * so forgive the slightly awkward and nonconforming
 * appearance.
 */

/*
 * Is this a device that supports enclosure services?
 *
 * It's a a pretty simple ruleset- if it is device type 0x0D (13), it's
 * an ENC device. If it happens to be an old UNISYS SEN device, we can
 * handle that too.
 */

#define	SAFTE_START	44
#define	SAFTE_END	50
#define	SAFTE_LEN	SAFTE_END-SAFTE_START

static enctyp
enc_type(void *buf, int buflen)
{
	unsigned char *iqd = buf;

	if (buflen < 8+SEN_ID_LEN)
		return (ENC_NONE);

	if ((iqd[0] & 0x1f) == T_ENCLOSURE) {
		if (STRNCMP(&iqd[8], SEN_ID, SEN_ID_LEN) == 0) {
			return (ENC_SEN);
		} else if ((iqd[2] & 0x7) > 2) {
			return (ENC_SES);
		} else {
			return (ENC_SES_SCSI2);
		}
		return (ENC_NONE);
	}

#ifdef	ENC_ENABLE_PASSTHROUGH
	if ((iqd[6] & 0x40) && (iqd[2] & 0x7) >= 2) {
		/*
		 * PassThrough Device.
		 */
		return (ENC_ENC_PASSTHROUGH);
	}
#endif

	/*
	 * The comparison is short for a reason-
	 * some vendors were chopping it short.
	 */

	if (buflen < SAFTE_END - 2) {
		return (ENC_NONE);
	}

	if (STRNCMP((char *)&iqd[SAFTE_START], "SAF-TE", SAFTE_LEN - 2) == 0) {
		return (ENC_SAFT);
	}
	return (ENC_NONE);
}

/*================== Enclosure Monitoring/Processing Daemon ==================*/
/**
 * \brief Queue an update request for a given action, if needed.
 *
 * \param enc		SES softc to queue the request for.
 * \param action	Action requested.
 */
void
enc_update_request(enc_softc_t *enc, uint32_t action)
{
	if ((enc->pending_actions & (0x1 << action)) == 0) {
		enc->pending_actions |= (0x1 << action);
		ENC_DLOG(enc, "%s: queing requested action %d\n",
		    __func__, action);
		if (enc->current_action == ENC_UPDATE_NONE)
			wakeup(enc->enc_daemon);
	} else {
		ENC_DLOG(enc, "%s: ignoring requested action %d - "
		    "Already queued\n", __func__, action);
	}
}

/**
 * \brief Invoke the handler of the highest priority pending
 *	  state in the SES state machine.
 *
 * \param enc  The SES instance invoking the state machine.
 */
static void
enc_fsm_step(enc_softc_t *enc)
{
	union ccb            *ccb;
	uint8_t              *buf;
	struct enc_fsm_state *cur_state;
	int		      error;
	
	ENC_DLOG(enc, "%s enter %p\n", __func__, enc);

	enc->current_action   = ffs(enc->pending_actions) - 1;
	enc->pending_actions &= ~(0x1 << enc->current_action);

	cur_state = &enc->enc_fsm_states[enc->current_action];

	buf = NULL;
	if (cur_state->buf_size != 0)
		buf = malloc(cur_state->buf_size, M_SCSIENC, M_WAITOK|M_ZERO);

	error = 0;
	ccb   = NULL;
	if (cur_state->fill != NULL) {
		ccb = cam_periph_getccb(enc->periph, CAM_PRIORITY_NORMAL);

		error = cur_state->fill(enc, cur_state, ccb, buf);
		if (error == 0) {
			error = cam_periph_runccb(ccb, cur_state->error,
						  ENC_CFLAGS,
						  ENC_FLAGS|SF_QUIET_IR, NULL);
		}
	}

	
	if (error == 0) {
		uint32_t resid;

		resid = 0;
		if (ccb != NULL)
			resid = ccb->csio.dxfer_len - ccb->csio.resid;

		cam_periph_unlock(enc->periph);
		cur_state->done(enc, cur_state, ccb, &buf, resid);
		cam_periph_lock(enc->periph);
	}

	ENC_DLOG(enc, "%s exit - result %d\n", __func__, error);
	ENC_FREE_AND_NULL(buf);
	if (ccb != NULL)
		xpt_release_ccb(ccb);
}

/**
 * \invariant Called with cam_periph mutex held.
 */
static void
enc_status_updater(void *arg)
{
	enc_softc_t *enc;

	enc = arg;
	if (enc->enc_vec.poll_status != NULL)
		enc->enc_vec.poll_status(enc);
}

static void
enc_daemon(void *arg)
{
	enc_softc_t *enc;

	enc = arg;

	cam_periph_lock(enc->periph);
	while ((enc->enc_flags & ENC_FLAG_SHUTDOWN) == 0) {
		if (enc->pending_actions == 0) {
			struct intr_config_hook *hook;

			/*
			 * Reset callout and msleep, or
			 * issue timed task completion
			 * status command.
			 */
			enc->current_action = ENC_UPDATE_NONE;

			/*
			 * We've been through our state machine at least
			 * once.  Allow the transition to userland.
			 */
			hook = &enc->enc_boot_hold_ch;
			if (hook->ich_func != NULL) {
				config_intrhook_disestablish(hook);
				hook->ich_func = NULL;
			}

			callout_reset(&enc->status_updater, 60*hz,
				      enc_status_updater, enc);

			cam_periph_sleep(enc->periph, enc->enc_daemon,
					 PUSER, "idle", 0);
		} else {
			enc_fsm_step(enc);
		}
	}
	enc->enc_daemon = NULL;
	cam_periph_unlock(enc->periph);
	kproc_exit(0);
}

static int
enc_kproc_init(enc_softc_t *enc)
{
	int result;

	callout_init_mtx(&enc->status_updater, enc->periph->sim->mtx, 0);

	result = kproc_create(enc_daemon, enc, &enc->enc_daemon, /*flags*/0,
			      /*stackpgs*/0, "enc_daemon%d",
			      enc->periph->unit_number);
	if (result == 0) {
		/* Do an initial load of all page data. */
		cam_periph_lock(enc->periph);
		enc->enc_vec.poll_status(enc);
		cam_periph_unlock(enc->periph);
	}
	return (result);
}
 
/**
 * \brief Interrupt configuration hook callback associated with
 *        enc_boot_hold_ch.
 *
 * Since interrupts are always functional at the time of enclosure
 * configuration, there is nothing to be done when the callback occurs.
 * This hook is only registered to hold up boot processing while initial
 * eclosure processing occurs.
 * 
 * \param arg  The enclosure softc, but currently unused in this callback.
 */
static void
enc_nop_confighook_cb(void *arg __unused)
{
}

static cam_status
enc_ctor(struct cam_periph *periph, void *arg)
{
	cam_status status = CAM_REQ_CMP_ERR;
	int err;
	enc_softc_t *enc;
	struct ccb_getdev *cgd;
	char *tname;

	cgd = (struct ccb_getdev *)arg;
	if (periph == NULL) {
		printf("enc_ctor: periph was NULL!!\n");
		goto out;
	}

	if (cgd == NULL) {
		printf("enc_ctor: no getdev CCB, can't register device\n");
		goto out;
	}

	enc = ENC_MALLOCZ(sizeof(*enc));
	if (enc == NULL) {
		printf("enc_ctor: Unable to probe new device. "
		       "Unable to allocate enc\n");				
		goto out;
	}
	enc->periph = periph;
	enc->current_action = ENC_UPDATE_NONE;

	enc->enc_type = enc_type(&cgd->inq_data, sizeof (cgd->inq_data));
	sx_init(&enc->enc_cache_lock, "enccache");

	switch (enc->enc_type) {
	case ENC_SES:
	case ENC_SES_SCSI2:
	case ENC_SES_PASSTHROUGH:
		err = ses_softc_init(enc, 1);
		break;
	case ENC_SAFT:
		err = safte_softc_init(enc, 1);
		break;
	case ENC_SEN:
	case ENC_NONE:
	default:
		ENC_FREE(enc);
		return (CAM_REQ_CMP_ERR);
	}

	if (err) {
		xpt_print(periph->path, "error %d initializing\n", err);
		goto out;
	}

	/*
	 * Hold off userland until we have made at least one pass
	 * through our state machine so that physical path data is
	 * present.
	 */
	enc->enc_boot_hold_ch.ich_func = enc_nop_confighook_cb;
	enc->enc_boot_hold_ch.ich_arg = enc;
	config_intrhook_establish(&enc->enc_boot_hold_ch);

	/*
	 * The softc field is set only once the enc is fully initialized
	 * so that we can rely on this field to detect partially
	 * initialized periph objects in the AC_FOUND_DEVICE handler.
	 */
	periph->softc = enc;

	cam_periph_unlock(periph);
	if (enc->enc_vec.poll_status != NULL) {
		err = enc_kproc_init(enc);
		if (err) {
			xpt_print(periph->path,
				  "error %d string enc_daemon\n", err);
			goto out;
		}
	}
	enc->enc_dev = make_dev(&enc_cdevsw, periph->unit_number,
	    UID_ROOT, GID_OPERATOR, 0600, "%s%d",
	    periph->periph_name, periph->unit_number);
	cam_periph_lock(periph);
	enc->enc_dev->si_drv1 = periph;

	enc->enc_flags |= ENC_FLAG_INITIALIZED;

	/*
	 * Add an async callback so that we get notified if this
	 * device goes away.
	 */
	xpt_register_async(AC_LOST_DEVICE, enc_async, periph, periph->path);

	switch (enc->enc_type) {
	default:
	case ENC_NONE:
		tname = "No ENC device";
		break;
	case ENC_SES_SCSI2:
		tname = "SCSI-2 ENC Device";
		break;
	case ENC_SES:
		tname = "SCSI-3 ENC Device";
		break;
        case ENC_SES_PASSTHROUGH:
		tname = "ENC Passthrough Device";
		break;
        case ENC_SEN:
		tname = "UNISYS SEN Device (NOT HANDLED YET)";
		break;
        case ENC_SAFT:
		tname = "SAF-TE Compliant Device";
		break;
	}
	xpt_announce_periph(periph, tname);
	status = CAM_REQ_CMP;

out:
	if (status != CAM_REQ_CMP)
		enc_dtor(periph);
	return (status);
}

