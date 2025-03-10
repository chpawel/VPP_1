/*************************************************************************
*  Copyright (C) 2007 by Bruno Chareyre <bruno.chareyre@imag.fr>         *
*  Copyright (C) 2008 by Janek Kozicki <cosurgi@berlios.de>              *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

#include "VppCohesiveFrictionalContactLaw.hpp"
#include<pkg/dem/ScGeom.hpp>
#include<core/Omega.hpp>
#include<core/Scene.hpp>
#include<pkg/common/Sphere.hpp>

//YADE_PLUGIN((CohesiveFrictionalContactLaw)(Law2_ScGeom6D_CohFrictPhys_CohesionMoment)(CohFrictMat)(CohFrictPhys)(Ip2_CohFrictMat_CohFrictMat_CohFrictPhys));
YADE_PLUGIN((VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment)(VppCohFrictMat)(VppIp2_VppCohFrictMat_VppCohFrictMat_CohFrictPhys)(VppIp2_CohFrictMat_VppCohFrictMat_CohFrictPhys)(VppIp2_CohFrictMat_CohFrictMat_CohFrictPhys));
CREATE_LOGGER(VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment);

Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::getPlasticDissipation() {return (Real) plasticDissipation;}
void VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::initPlasticDissipation(Real initVal) {plasticDissipation.reset(); plasticDissipation+=initVal;}

Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::normElastEnergy()
{
	Real normEnergy=0;
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		CohFrictPhys* phys = YADE_CAST<CohFrictPhys*>(I->phys.get());
		if (phys) {
			normEnergy += 0.5*(phys->normalForce.squaredNorm()/phys->kn);
		}
	}
	return normEnergy;
}
Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::shearElastEnergy()
{
	Real shearEnergy=0;
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		CohFrictPhys* phys = YADE_CAST<CohFrictPhys*>(I->phys.get());
		if (phys) {
			shearEnergy += 0.5*(phys->shearForce.squaredNorm()/phys->ks);
		}
	}
	return shearEnergy;
}

Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::bendingElastEnergy()
{
	Real bendingEnergy=0;
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		CohFrictPhys* phys = YADE_CAST<CohFrictPhys*>(I->phys.get());
		if (phys) {
			bendingEnergy += 0.5*(phys->moment_bending.squaredNorm()/phys->kr);
		}
	}
	return bendingEnergy;
}

Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::twistElastEnergy()
{
	Real twistEnergy=0;
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		CohFrictPhys* phys = YADE_CAST<CohFrictPhys*>(I->phys.get());
		if (phys) {
			twistEnergy += 0.5*(phys->moment_twist.squaredNorm()/phys->ktw);
		}
	}
	return twistEnergy;
}

Real VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::totalElastEnergy()
{
	Real totalEnergy=0;
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		CohFrictPhys* phys = YADE_CAST<CohFrictPhys*>(I->phys.get());
		if (phys) {
			totalEnergy += 0.5*(phys->normalForce.squaredNorm()/phys->kn);
			totalEnergy += 0.5*(phys->shearForce.squaredNorm()/phys->ks);
			totalEnergy += 0.5*(phys->moment_bending.squaredNorm()/phys->kr);
			totalEnergy += 0.5*(phys->moment_twist.squaredNorm()/phys->ktw);
		}
	}
	return totalEnergy;
}



bool VppLaw2_ScGeom6D_CohFrictPhys_CohesionMoment::go(shared_ptr<IGeom>& ig, shared_ptr<IPhys>& ip, Interaction* contact)
{
	const Real& dt = scene->dt;
	const int &id1 = contact->getId1();
	const int &id2 = contact->getId2();
	ScGeom6D* geom  = YADE_CAST<ScGeom6D*> (ig.get());
	CohFrictPhys* phys = YADE_CAST<CohFrictPhys*> (ip.get());
	Vector3r& shearForce    = phys->shearForce;

	if (contact->isFresh(scene)) shearForce   = Vector3r::Zero();
	Real un     = geom->penetrationDepth;
	Real Fn    = phys->kn*(un-phys->unp);

	if (phys->fragile && (-Fn)> phys->normalAdhesion) {
		// BREAK due to tension
		return false;
	} else {
		if ((-Fn)> phys->normalAdhesion) {//normal plasticity
			Fn=-phys->normalAdhesion;
			phys->unp = un+phys->normalAdhesion/phys->kn;
			if (phys->unpMax>=0 && -phys->unp>phys->unpMax)  // Actually unpMax should be defined as a function of the average particule sizes for instance
				return false;
		}
		phys->normalForce = Fn*geom->normal;
		State* de1 = Body::byId(id1,scene)->state.get();
		State* de2 = Body::byId(id2,scene)->state.get();
		///////////////////////// CREEP START ///////////
		if (shear_creep) shearForce -= phys->ks*(shearForce*dt/creep_viscosity);
		///////////////////////// CREEP END ////////////

		Vector3r& shearForce = geom->rotate(phys->shearForce);
		const Vector3r& dus = geom->shearIncrement();

		//Linear elasticity giving "trial" shear force
		shearForce -= phys->ks*dus;
		
		
		Real Fs = phys->shearForce.norm();
		Real maxFs = phys->shearAdhesion;

		if (!phys->cohesionDisablesFriction || maxFs==0)
			maxFs += Fn*phys->tangensOfFrictionAngle;
		maxFs = std::max((Real) 0, maxFs);

		//obliczanie pracy 
		Real dW = 0;
		Real ds =  (Fs-maxFs)/phys->ks;
		if (ds > 0 ){
				dW  = ds * maxFs;  
		} 
				
		if (Fs  > maxFs) {//Plasticity condition on shear force
			if (phys->fragile && !phys->cohesionBroken) {
				if (shearBreaksCohesion) {//PH
					phys->SetBreakingState();
					maxFs = max((Real) 0, Fn*phys->tangensOfFrictionAngle);
				}//PH
				else {
				shearForce = Vector3r::Zero();//PH
				}
			}

		//ponowne obliczenie pracy jak zachodzi taka koniecznosc bo spelnbiony warunek plastycznosci 
//		Real ds =  (Fs-maxFs)/phys->ks;
//		if (ds > 0 ){
//			dW  = ds * maxFs;  
//		}else
//		{	
//			dW=0;
//		} 
		//koniec obliczania pracy			
			
			maxFs = maxFs / Fs;
			Vector3r trialForce=shearForce;
			shearForce *= maxFs;
			if (scene->trackEnergy){
				Real sheardissip=((1/phys->ks)*(trialForce-shearForce))/*plastic disp*/ .dot(shearForce)/*active force*/;
				if(sheardissip>0) scene->energy->add(sheardissip,"shearDissip",shearDissipIx,/*reset*/false);}
			if (Fn<0)  phys->normalForce = Vector3r::Zero();//Vector3r::Zero() //zmiana PCH kochezja zeby sie nie rozrywala
		
		//if(id1==1380){
		//	cout << " maxFs=" << maxFs << endl;
		//}

		}


		//cout << scene->iter << endl;
		//if(id1==3631){
		//	cout << "time= "<< scene->time << " id1= " << id1 << " id2= " << id2 << " shearForce= " << shearForce <<  endl; 
		//	cout << " ks=" << phys->ks  << " ds=" << ds <<  endl;
		//	cout << " dW=" << dW <<  endl;
		//}

		//Apply the force
		applyForceAtContactPoint(-phys->normalForce-shearForce, geom->contactPoint, id1, de1->se3.position, id2, de2->se3.position + (scene->isPeriodic ? scene->cell->intrShiftPos(contact->cellDist): Vector3r::Zero()));

		//Zapisanie pracy do Interakcji i state
		//cout << "id1="<< id1 <<" id2="<< id2 << " dW=" << dW << " ds=" << ds << endl;

		if (!contact->isFresh(scene)){ //tylko w momencie gdy kontakt nie jest nowy zapisujemy prace
			de1->work += dW/2;
			de2->work += dW/2;
		
			//zapis pracy do interakcji
			contact->work = dW;
			contact->workAccum+=dW;
		}
		//cout << "dWInt =" << contact->work << " workAccum=" << contact->workAccum << endl<< endl;

		/// Moment law  ///
		if (phys->momentRotationLaw && (!phys->cohesionBroken || always_use_moment_law)) {
			if (!useIncrementalForm){
				if (twist_creep) {
					Real viscosity_twist = creep_viscosity * std::pow((2 * std::min(geom->radius1,geom->radius2)),2) / 16.0;
					Real angle_twist_creeped = geom->getTwist() * (1 - dt/viscosity_twist);
					Quaternionr q_twist(AngleAxisr(geom->getTwist(),geom->normal));
					Quaternionr q_twist_creeped(AngleAxisr(angle_twist_creeped,geom->normal));
					Quaternionr q_twist_delta(q_twist_creeped * q_twist.conjugate());
					geom->twistCreep = geom->twistCreep * q_twist_delta;
				}
				phys->moment_twist = (geom->getTwist()*phys->ktw)*geom->normal;
				phys->moment_bending = geom->getBending() * phys->kr;
			}	
			else{ // Use incremental formulation to compute moment_twis and moment_bending (no twist_creep is applied)
				if (twist_creep) throw std::invalid_argument("Law2_ScGeom6D_CohFrictPhys_CohesionMoment: no twis creep is included if the incremental form for the rotations is used.");
				Vector3r relAngVel = geom->getRelAngVel(de1,de2,dt);
				// *** Bending ***//
				Vector3r relAngVelBend = relAngVel - geom->normal.dot(relAngVel)*geom->normal; // keep only the bending part
				Vector3r relRotBend = relAngVelBend*dt; // relative rotation due to rolling behaviour	
				// incremental formulation for the bending moment (as for the shear part)
				Vector3r& momentBend = phys->moment_bending;
				momentBend = geom->rotate(momentBend); // rotate moment vector (updated)
				momentBend = momentBend-phys->kr*relRotBend;
				// ----------------------------------------------------------------------------------------
				// *** Torsion ***//
				Vector3r relAngVelTwist = geom->normal.dot(relAngVel)*geom->normal;
				Vector3r relRotTwist = relAngVelTwist*dt; // component of relative rotation along n  FIXME: sign?
				// incremental formulation for the torsional moment
				Vector3r& momentTwist = phys->moment_twist;
				momentTwist = geom->rotate(momentTwist); // rotate moment vector (updated)
				momentTwist = momentTwist-phys->ktw*relRotTwist; // FIXME: sign?
			}
			/// Plasticity ///
			// limit rolling moment to the plastic value, if required
			if (phys->maxRollPl>=0.){ // do we want to apply plasticity?
				Real RollMax = phys->maxRollPl*phys->normalForce.norm();
				if (!useIncrementalForm) LOG_WARN("If :yref:`Law2_ScGeom6D_CohFrictPhys_CohesionMoment::useIncrementalForm` is false, then plasticity will not be applied correctly (the total formulation would not reproduce irreversibility).");
				Real scalarRoll = phys->moment_bending.norm();
				if (scalarRoll>RollMax){ // fix maximum rolling moment
					Real ratio = RollMax/scalarRoll;
					phys->moment_bending *= ratio;
					if (scene->trackEnergy){
						Real bendingdissip=((1/phys->kr)*(scalarRoll-RollMax)*RollMax)/*active force*/;
						if(bendingdissip>0) scene->energy->add(bendingdissip,"bendingDissip",bendingDissipIx,/*reset*/false);}
				}
			}
			// limit twisting moment to the plastic value, if required
			if (phys->maxTwistPl>=0.){ // do we want to apply plasticity?
				Real TwistMax = phys->maxTwistPl*phys->normalForce.norm();
				if (!useIncrementalForm) LOG_WARN("If :yref:`Law2_ScGeom6D_CohFrictPhys_CohesionMoment::useIncrementalForm` is false, then plasticity will not be applied correctly (the total formulation would not reproduce irreversibility).");
				Real scalarTwist= phys->moment_twist.norm();
				if (scalarTwist>TwistMax){ // fix maximum rolling moment
					Real ratio = TwistMax/scalarTwist;
					phys->moment_twist *= ratio;
					if (scene->trackEnergy){
						Real twistdissip=((1/phys->ktw)*(scalarTwist-TwistMax)*TwistMax)/*active force*/;
						if(twistdissip>0) scene->energy->add(twistdissip,"twistDissip",twistDissipIx,/*reset*/false);}
				}	
			}
			// Apply moments now
			Vector3r moment = phys->moment_twist + phys->moment_bending;
			scene->forces.addTorque(id1,-moment);
			scene->forces.addTorque(id2, moment);			
		}
		/// Moment law END       ///
	}
	return true;
}


void VppIp2_VppCohFrictMat_VppCohFrictMat_CohFrictPhys::go(const shared_ptr<Material>& b1    // VppCohFrictMat
                                        , const shared_ptr<Material>& b2 // VppCohFrictMat
                                        , const shared_ptr<Interaction>& interaction)
{
	VppCohFrictMat* sdec1 = static_cast<VppCohFrictMat*>(b1.get());
	VppCohFrictMat* sdec2 = static_cast<VppCohFrictMat*>(b2.get());
	ScGeom6D* geom = YADE_CAST<ScGeom6D*>(interaction->geom.get());

	//Create cohesive interractions only once
	if (setCohesionNow && cohesionDefinitionIteration==-1) cohesionDefinitionIteration=scene->iter;
	if (setCohesionNow && cohesionDefinitionIteration!=-1 && cohesionDefinitionIteration!=scene->iter) {
		cohesionDefinitionIteration = -1;
		setCohesionNow = 0;}

	if (geom) {
		if (!interaction->phys) {
			interaction->phys = shared_ptr<CohFrictPhys>(new CohFrictPhys());
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			Real Ea 	= sdec1->young;
			Real Eb 	= sdec2->young;
			Real Va 	= sdec1->poisson;
			Real Vb 	= sdec2->poisson;
			Real Da 	= geom->radius1;
			Real Db 	= geom->radius2;
			Real fa 	= sdec1->frictionAngle;
			Real fb 	= sdec2->frictionAngle;

			Real Kn = 2.0*Ea*Da*Eb*Db/(Ea*Da+Eb*Db);//harmonic average of two stiffnesses

			//changing stiffness when there is any penetration
			int idB1 = interaction->id1;
			int idB2 = interaction->id2;

			const shared_ptr<Body>& body1 = Body::byId(idB1,scene);
			const shared_ptr<Body>& body2 = Body::byId(idB2,scene);

			
			VppState* st1 = static_cast<VppState*>(body1->state.get());
			VppState* st2 = static_cast<VppState*>(body2->state.get());
			//get actual pos		
			Vector3r pos1 = st1->pos;
			Vector3r pos2 = st2->pos;
			
			Shape* sh1 =  static_cast<Shape*>(body1->shape.get());
			Sphere* sphere1 = static_cast<Sphere*>(sh1);		
			if(((pos1-pos2).norm()-2*sphere1->radius)<0){
				Kn=Kn*sdec1->knRatio;
			}
			

			// harmonic average of alphas parameters
			Real AlphaKr = 2.0*sdec1->alphaKr*sdec2->alphaKr/(sdec1->alphaKr+sdec2->alphaKr);
			Real AlphaKtw;
			if (sdec1->alphaKtw && sdec2->alphaKtw) AlphaKtw = 2.0*sdec1->alphaKtw*sdec2->alphaKtw/(sdec1->alphaKtw+sdec2->alphaKtw);
			else AlphaKtw=0;

			Real Ks;
			if (Va && Vb) Ks = 2.0*Ea*Da*Va*Eb*Db*Vb/(Ea*Da*Va+Eb*Db*Vb);//harmonic average of two stiffnesses with ks=V*kn for each sphere
			else Ks=0;

			contactPhysics->kr = Da*Db*Ks*AlphaKr;
			contactPhysics->ktw = Da*Db*Ks*AlphaKtw;
			contactPhysics->tangensOfFrictionAngle		= std::tan(std::min(fa,fb));

			if ((setCohesionOnNewContacts || setCohesionNow) && sdec1->isCohesive && sdec2->isCohesive)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(Db, Da),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(Db, Da),2);
				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
			}
			contactPhysics->kn = Kn;
			contactPhysics->ks = Ks;

			contactPhysics->maxRollPl = min(sdec1->etaRoll*Da,sdec2->etaRoll*Db);
			contactPhysics->maxTwistPl = min(sdec1->etaTwist*Da,sdec2->etaTwist*Db);
			contactPhysics->momentRotationLaw=(sdec1->momentRotationLaw && sdec2->momentRotationLaw);
		}
		else {// !isNew, but if setCohesionNow, all contacts are initialized like if they were newly created
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			if ((setCohesionNow && sdec1->isCohesive && sdec2->isCohesive) || contactPhysics->initCohesion)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(geom->radius2, geom->radius1),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(geom->radius2, geom->radius1),2);

				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
				contactPhysics->initCohesion=false;
			}
		}
	}
};

void VppIp2_CohFrictMat_VppCohFrictMat_CohFrictPhys::go(const shared_ptr<Material>& b1    // CohFrictMat
                                        , const shared_ptr<Material>& b2 // VppCohFrictMat
                                        , const shared_ptr<Interaction>& interaction)
{
	CohFrictMat* sdec1 = static_cast<CohFrictMat*>(b1.get());
	VppCohFrictMat* sdec2 = static_cast<VppCohFrictMat*>(b2.get());
	ScGeom6D* geom = YADE_CAST<ScGeom6D*>(interaction->geom.get());

	//Create cohesive interractions only once
	if (setCohesionNow && cohesionDefinitionIteration==-1) cohesionDefinitionIteration=scene->iter;
	if (setCohesionNow && cohesionDefinitionIteration!=-1 && cohesionDefinitionIteration!=scene->iter) {
		cohesionDefinitionIteration = -1;
		setCohesionNow = 0;}

	if (geom) {
		if (!interaction->phys) {
			interaction->phys = shared_ptr<CohFrictPhys>(new CohFrictPhys());
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			Real Ea 	= sdec1->young;
			Real Eb 	= sdec2->young;
			Real Va 	= sdec1->poisson;
			Real Vb 	= sdec2->poisson;
			Real Da 	= geom->radius1;
			Real Db 	= geom->radius2;
			Real fa 	= sdec1->frictionAngle;
			Real fb 	= sdec2->frictionAngle;
			Real Kn = 2.0*Ea*Da*Eb*Db/(Ea*Da+Eb*Db);//harmonic average of two stiffnesses

			// harmonic average of alphas parameters
			Real AlphaKr = 2.0*sdec1->alphaKr*sdec2->alphaKr/(sdec1->alphaKr+sdec2->alphaKr);
			Real AlphaKtw;
			if (sdec1->alphaKtw && sdec2->alphaKtw) AlphaKtw = 2.0*sdec1->alphaKtw*sdec2->alphaKtw/(sdec1->alphaKtw+sdec2->alphaKtw);
			else AlphaKtw=0;

			Real Ks;
			if (Va && Vb) Ks = 2.0*Ea*Da*Va*Eb*Db*Vb/(Ea*Da*Va+Eb*Db*Vb);//harmonic average of two stiffnesses with ks=V*kn for each sphere
			else Ks=0;

			contactPhysics->kr = Da*Db*Ks*AlphaKr;
			contactPhysics->ktw = Da*Db*Ks*AlphaKtw;
			contactPhysics->tangensOfFrictionAngle		= std::tan(std::min(fa,fb));  //PCH change from min to max - friction from granular zmiana na chwile

			if ((setCohesionOnNewContacts || setCohesionNow) && sdec1->isCohesive && sdec2->isCohesive)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(Db, Da),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(Db, Da),2);
				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
			}
			contactPhysics->kn = Kn;
			contactPhysics->ks = Ks;

			contactPhysics->maxRollPl = min(sdec1->etaRoll*Da,sdec2->etaRoll*Db);
			contactPhysics->maxTwistPl = min(sdec1->etaTwist*Da,sdec2->etaTwist*Db);
			contactPhysics->momentRotationLaw=(sdec1->momentRotationLaw && sdec2->momentRotationLaw);
		}
		else {// !isNew, but if setCohesionNow, all contacts are initialized like if they were newly created
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			if ((setCohesionNow && sdec1->isCohesive && sdec2->isCohesive) || contactPhysics->initCohesion)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(geom->radius2, geom->radius1),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(geom->radius2, geom->radius1),2);

				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
				contactPhysics->initCohesion=false;
			}
		}
	}
};

// na razie sprawdzam
void VppIp2_CohFrictMat_CohFrictMat_CohFrictPhys::go(const shared_ptr<Material>& b1    // CohFrictMat
                                        , const shared_ptr<Material>& b2 // CohFrictMat
                                        , const shared_ptr<Interaction>& interaction)
{
	CohFrictMat* sdec1 = static_cast<CohFrictMat*>(b1.get());
	CohFrictMat* sdec2 = static_cast<CohFrictMat*>(b2.get());
	ScGeom6D* geom = YADE_CAST<ScGeom6D*>(interaction->geom.get());

	//Create cohesive interractions only once
	if (setCohesionNow && cohesionDefinitionIteration==-1) cohesionDefinitionIteration=scene->iter;
	if (setCohesionNow && cohesionDefinitionIteration!=-1 && cohesionDefinitionIteration!=scene->iter) {
		cohesionDefinitionIteration = -1;
		setCohesionNow = 0;}

	if (geom) {
		if (!interaction->phys) {
			interaction->phys = shared_ptr<CohFrictPhys>(new CohFrictPhys());
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			Real Ea 	= sdec1->young;
			Real Eb 	= sdec2->young;
			Real Va 	= sdec1->poisson;
			Real Vb 	= sdec2->poisson;
			Real Da 	= geom->radius1;
			Real Db 	= geom->radius2;
			Real fa 	= sdec1->frictionAngle;
			Real fb 	= sdec2->frictionAngle;
			Real Kn = 2.0*Ea*Da*Eb*Db/(Ea*Da+Eb*Db);//harmonic average of two stiffnesses

			// harmonic average of alphas parameters
			Real AlphaKr = 2.0*sdec1->alphaKr*sdec2->alphaKr/(sdec1->alphaKr+sdec2->alphaKr);
			Real AlphaKtw;
			if (sdec1->alphaKtw && sdec2->alphaKtw) AlphaKtw = 2.0*sdec1->alphaKtw*sdec2->alphaKtw/(sdec1->alphaKtw+sdec2->alphaKtw);
			else AlphaKtw=0;

			Real Ks;
			if (Va && Vb) Ks = 2.0*Ea*Da*Va*Eb*Db*Vb/(Ea*Da*Va+Eb*Db*Vb);//harmonic average of two stiffnesses with ks=V*kn for each sphere
			else Ks=0;

			contactPhysics->kr = Da*Db*Ks*AlphaKr;
			contactPhysics->ktw = Da*Db*Ks*AlphaKtw;
			contactPhysics->tangensOfFrictionAngle		= std::tan(std::min(fa,fb));

			if ((setCohesionOnNewContacts || setCohesionNow) && sdec1->isCohesive && sdec2->isCohesive)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(Db, Da),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(Db, Da),2);
				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
			}
			contactPhysics->kn = Kn;
			contactPhysics->ks = Ks;

			contactPhysics->maxRollPl = min(sdec1->etaRoll*Da,sdec2->etaRoll*Db);
			contactPhysics->maxTwistPl = min(sdec1->etaTwist*Da,sdec2->etaTwist*Db);
			contactPhysics->momentRotationLaw=(sdec1->momentRotationLaw && sdec2->momentRotationLaw);
		}
		else {// !isNew, but if setCohesionNow, all contacts are initialized like if they were newly created
			CohFrictPhys* contactPhysics = YADE_CAST<CohFrictPhys*>(interaction->phys.get());
			if ((setCohesionNow && sdec1->isCohesive && sdec2->isCohesive) || contactPhysics->initCohesion)
			{
				contactPhysics->cohesionBroken = false;
				contactPhysics->normalAdhesion = std::min(sdec1->normalCohesion,sdec2->normalCohesion)*pow(std::min(geom->radius2, geom->radius1),2);
				contactPhysics->shearAdhesion = std::min(sdec1->shearCohesion,sdec2->shearCohesion)*pow(std::min(geom->radius2, geom->radius1),2);

				geom->initRotations(*(Body::byId(interaction->getId1(),scene)->state),*(Body::byId(interaction->getId2(),scene)->state));
				contactPhysics->fragile=(sdec1->fragile || sdec2->fragile);
				contactPhysics->initCohesion=false;
			}
		}
	}
};




