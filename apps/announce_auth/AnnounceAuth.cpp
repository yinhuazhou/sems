/*
 * $Id$
 *
 * Copyright (C) 2002-2003 Fhg Fokus
 * Copyright (C) 2006 iptego GmbH
 *
 * This file is part of sems, a free SIP media server.
 *
 * sems is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For a license to use the sems software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * sems is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "AnnounceAuth.h"
#include "AmConfig.h"
#include "AmUtils.h"

#include "sems.h"
#include "log.h"

#include "ampi/UACAuthAPI.h"
#include "AmUAC.h"
#include "AmPlugIn.h"

#define MOD_NAME "announce_auth"

EXPORT_SESSION_FACTORY(AnnounceAuthFactory,MOD_NAME);

string AnnounceAuthFactory::AnnouncePath;
string AnnounceAuthFactory::AnnounceFile;

AnnounceAuthFactory::AnnounceAuthFactory(const string& _app_name)
  : AmSessionFactory(_app_name)
{
}

int AnnounceAuthFactory::onLoad()
{
    AmConfigReader cfg;
    if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf")))
	return -1;

    // get application specific global parameters
    configureModule(cfg);

    AnnouncePath = cfg.getParameter("announce_path",ANNOUNCE_PATH);
    if( !AnnouncePath.empty() 
	&& AnnouncePath[AnnouncePath.length()-1] != '/' )
	AnnouncePath += "/";

    AnnounceFile = cfg.getParameter("default_announce",ANNOUNCE_FILE);

    string announce_file = AnnouncePath + AnnounceFile;
    if(!file_exists(announce_file)){
	ERROR("default file for ann_b2b module does not exist ('%s').\n",
	      announce_file.c_str());
	return -1;
    }


	auth_realm = cfg.getParameter("auth_realm", "");
	auth_user  = cfg.getParameter("auth_user",  "");
	auth_pwd   = cfg.getParameter("auth_pwd", "");

    uac_auth_f = AmPlugIn::instance()->getFactory4Seh("uac_auth");
    DBG("uac_auth_f == 0x%.16lX\n",(unsigned long)uac_auth_f);

	dialer.set_dial(cfg.getParameter("dial_ruri","default ruri"),
					cfg.getParameter("dial_from","default from"),
					cfg.getParameter("dial_fromuri","default fromuri"),
					cfg.getParameter("dial_to","default to"));

	dialer.start();

    return 0;
}

AmSession* AnnounceAuthFactory::onInvite(const AmSipRequest& req)
{
    string announce_path = AnnouncePath;
    string announce_file = announce_path + req.domain 
	+ "/" + req.user + ".wav";

    DBG("trying '%s'\n",announce_file.c_str());
    if(file_exists(announce_file))
	goto end;

    announce_file = announce_path + req.user + ".wav";
    DBG("trying '%s'\n",announce_file.c_str());
    if(file_exists(announce_file))
	goto end;

    announce_file = AnnouncePath + AnnounceFile;
    
end:
	AnnounceAuthDialog* dlg = new AnnounceAuthDialog(announce_file,
													 auth_realm, 
													 auth_user,
													 auth_pwd);

    if (uac_auth_f != NULL) {
		DBG("UAC Auth enabled for new announcement session.\n");
		AmSessionEventHandler* h = uac_auth_f->getHandler(dlg);
		if (h != NULL )
			dlg->addHandler(h);
	}

    return dlg;
}

AnnounceAuthDialog::AnnounceAuthDialog(const string& filename,
									   const string& auth_realm, 
									   const string& auth_user,
									   const string& auth_pwd)
    : filename(filename), 
	  credentials(auth_realm, auth_user, auth_pwd)
{

}

UACAuthCred* AnnounceAuthDialog::getCredentials() {
	return &credentials;
}

AnnounceAuthDialog::~AnnounceAuthDialog()
{
}

void AnnounceAuthDialog::onSessionStart(const AmSipRequest& req)
{
    DBG("AnnounceAuthDialog::onSessionStart\n");
    startSession();
}

void AnnounceAuthDialog::onSessionStart(const AmSipReply& rep)
{
    DBG("AnnounceAuthDialog::onSessionStart (SEMS originator mode)\n");
    startSession();
}

void AnnounceAuthDialog::startSession(){
    if(wav_file.open(filename,AmAudioFile::Read))
	throw string("AnnounceAuthDialog::onSessionStart: Cannot open file\n");
    
    setOutput(&wav_file);
}

void AnnounceAuthDialog::onBye(const AmSipRequest& req)
{
    DBG("onBye: stopSession\n");
    setStopped();
}


void AnnounceAuthDialog::process(AmEvent* event)
{

    AmAudioEvent* audio_event = dynamic_cast<AmAudioEvent*>(event);
    if(audio_event && (audio_event->event_id == AmAudioEvent::cleared)){
	dlg.bye();
	setStopped();
	return;
    }

    AmSession::process(event);
}

void DialerThread::set_dial(const string& r, const string& f, 
							const string& fu, const string& t) {
	r_uri = r;
	from = f;
	from_uri = fu;
	to = t;
}

void DialerThread::run() {
	sleep(15); // wait for sems to completely start up
	while (!is_stopped()) {		
		DBG("dialing...");
		AmUAC::dialout("blibla", "announce_auth", 
					   r_uri, from, from_uri, to);
		// every 10 minutes
		sleep(100);

	} 
}

void DialerThread::on_stop() {
	DBG("stopping...\n");
}
