//HEAD_DSPH
/*
 <DUALSPHYSICS>  Copyright (c) 2017 by Dr Jose M. Dominguez et al. (see http://dual.sphysics.org/index.php/developers/). 

 EPHYSLAB Environmental Physics Laboratory, Universidade de Vigo, Ourense, Spain.
 School of Mechanical, Aerospace and Civil Engineering, University of Manchester, Manchester, U.K.

 This file is part of DualSPHysics. 

 DualSPHysics is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License 
 as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
 
 DualSPHysics is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details. 

 You should have received a copy of the GNU Lesser General Public License along with DualSPHysics. If not, see <http://www.gnu.org/licenses/>. 
*/

/// \file JSphCpuSingle.cpp \brief Implements the class \ref JSphCpuSingle.

#include "JSphCpuSingle.h"
#include "JCellDivCpuSingle.h"
#include "JArraysCpu.h"
#include "JSphMk.h"
#include "Functions.h"
#include "FunctionsMath.h"
#include "JXml.h"
#include "JSphMotion.h"
#include "JPartsLoad4.h"
#include "JSphVisco.h"
#include "JWaveGen.h"
#include "JDamping.h"
#include "JTimeOut.h"
#include "JTimeControl.h"
#include "JGaugeSystem.h"
#include <climits>

using namespace std;
//==============================================================================
/// Constructor.
//==============================================================================
JSphCpuSingle::JSphCpuSingle():JSphCpu(false){
  ClassName="JSphCpuSingle";
  CellDivSingle=NULL;
  PartsLoaded=NULL;
}

//==============================================================================
/// Destructor.
//==============================================================================
JSphCpuSingle::~JSphCpuSingle(){
  delete CellDivSingle; CellDivSingle=NULL;
  delete PartsLoaded;   PartsLoaded=NULL;
}

//==============================================================================
/// Return memory reserved in CPU.
/// Devuelve la memoria reservada en cpu.
//==============================================================================
llong JSphCpuSingle::GetAllocMemoryCpu()const{  
  llong s=JSphCpu::GetAllocMemoryCpu();
  //-Allocated in other objects.
  if(CellDivSingle)s+=CellDivSingle->GetAllocMemory();
  if(PartsLoaded)s+=PartsLoaded->GetAllocMemory();
  return(s);
}

//==============================================================================
/// Update maximum values of memory, particles & cells.
/// Actualiza los valores maximos de memory, particles y cells.
//==============================================================================
void JSphCpuSingle::UpdateMaxValues(){
  MaxParticles=max(MaxParticles,Np);
  if(CellDivSingle)MaxCells=max(MaxCells,CellDivSingle->GetNct());
  llong m=GetAllocMemoryCpu();
  MaxMemoryCpu=max(MaxMemoryCpu,m);
}

//==============================================================================
/// Load the execution configuration.
/// Carga la configuracion de ejecucion.
//==============================================================================
void JSphCpuSingle::LoadConfig(JCfgRun *cfg){
  const char met[]="LoadConfig";
  //-Load OpenMP configuraction. | Carga configuracion de OpenMP.
  ConfigOmp(cfg);
  //-Load basic general configuraction. | Carga configuracion basica general.
  JSph::LoadConfig(cfg);
  //-Checks compatibility of selected options.
  Log->Print("**Special case configuration is loaded");
}

//==============================================================================
/// Load particles of case and process.
/// Carga particulas del caso a procesar.
//==============================================================================
void JSphCpuSingle::LoadCaseParticles(){
  Log->Print("Loading initial state of particles...");
  PartsLoaded=new JPartsLoad4(true);
  PartsLoaded->LoadParticles(DirCase,CaseName,PartBegin,PartBeginDir);
  PartsLoaded->CheckConfig(CaseNp,CaseNfixed,CaseNmoving,CaseNfloat,CaseNfluid,PeriX,PeriY,PeriZ);
  Log->Printf("Loaded particles: %u",PartsLoaded->GetCount());
  //-Collect information of loaded particles. | Recupera informacion de las particulas cargadas.
  Simulate2D=PartsLoaded->GetSimulate2D();
  Simulate2DPosY=PartsLoaded->GetSimulate2DPosY();
  if(Simulate2D&&PeriY)RunException("LoadCaseParticles","Cannot use periodic conditions in Y with 2D simulations");
  CasePosMin=PartsLoaded->GetCasePosMin();
  CasePosMax=PartsLoaded->GetCasePosMax();

  //-Calculate actual limits of simulation. | Calcula limites reales de la simulacion.
  if(PartsLoaded->MapSizeLoaded())PartsLoaded->GetMapSize(MapRealPosMin,MapRealPosMax);
  else{
    PartsLoaded->CalculeLimits(double(H)*BORDER_MAP,Dp/2.,PeriX,PeriY,PeriZ,MapRealPosMin,MapRealPosMax);
    ResizeMapLimits();
  }
  if(PartBegin){
    PartBeginTimeStep=PartsLoaded->GetPartBeginTimeStep();
    PartBeginTotalNp=PartsLoaded->GetPartBeginTotalNp();
  }
  Log->Print(string("MapRealPos(final)=")+fun::Double3gRangeStr(MapRealPosMin,MapRealPosMax));
  MapRealSize=MapRealPosMax-MapRealPosMin;
  Log->Print("**Initial state of particles is loaded");

  //-Configure limits of periodic axes. | Configura limites de ejes periodicos.
  if(PeriX)PeriXinc.x=-MapRealSize.x;
  if(PeriY)PeriYinc.y=-MapRealSize.y;
  if(PeriZ)PeriZinc.z=-MapRealSize.z;
  //-Calculate simulation limits with periodic boundaries. | Calcula limites de simulacion con bordes periodicos.
  Map_PosMin=MapRealPosMin; Map_PosMax=MapRealPosMax;
  float dosh=float(H*2);
  if(PeriX){ Map_PosMin.x=Map_PosMin.x-dosh;  Map_PosMax.x=Map_PosMax.x+dosh; }
  if(PeriY){ Map_PosMin.y=Map_PosMin.y-dosh;  Map_PosMax.y=Map_PosMax.y+dosh; }
  if(PeriZ){ Map_PosMin.z=Map_PosMin.z-dosh;  Map_PosMax.z=Map_PosMax.z+dosh; }
  Map_Size=Map_PosMax-Map_PosMin;
  //-Saves initial domain in a VTK file (CasePosMin/Max, MapRealPosMin/Max and Map_PosMin/Max).
  SaveInitialDomainVtk();
}

//==============================================================================
/// Configuration of current domain.
/// Configuracion del dominio actual.
//==============================================================================
void JSphCpuSingle::ConfigDomain(){
  const char* met="ConfigDomain";
  //-Calculate number of particles. | Calcula numero de particulas.
  Np=PartsLoaded->GetCount(); Npb=CaseNpb; NpbOk=Npb;
  //-Allocates fixed memory for moving & floating particles. | Reserva memoria fija para moving y floating.
  AllocCpuMemoryFixed();
  //-Allocates memory in CPU for particles. | Reserva memoria en Cpu para particulas.
  AllocCpuMemoryParticles(Np,0);

  //-Copy particle values. | Copia datos de particulas.
  ReserveBasicArraysCpu();
  memcpy(Posc,PartsLoaded->GetPos(),sizeof(tdouble3)*Np);
  memcpy(Idpc,PartsLoaded->GetIdp(),sizeof(unsigned)*Np);
  memcpy(Velrhopc,PartsLoaded->GetVelRhop(),sizeof(tfloat4)*Np);

  //-Calculate floating radius. | Calcula radio de floatings.
  if(CaseNfloat && PeriActive!=0 && !PartBegin)CalcFloatingRadius(Np,Posc,Idpc);

  //-Load particle code. | Carga code de particulas.
  LoadCodeParticles(Np,Idpc,Codec);

  //-Runs initialization operations from XML.
  RunInitialize(Np,Npb,Posc,Idpc,Codec,Velrhopc);

  //-Computes MK domain for boundary and fluid particles.
  MkInfo->ComputeMkDomains(Np,Posc,Codec);

  //-Free memory of PartsLoaded. | Libera memoria de PartsLoaded.
  delete PartsLoaded; PartsLoaded=NULL;
  //-Apply configuration of CellOrder. | Aplica configuracion de CellOrder.
  ConfigCellOrder(CellOrder,Np,Posc,Velrhopc);

  //-Configure cells division. | Configura division celdas.
  ConfigCellDivision();
  //-Establish local simulation domain inside of Map_Cells & calculate DomCellCode. | Establece dominio de simulacion local dentro de Map_Cells y calcula DomCellCode.
  SelecDomain(TUint3(0,0,0),Map_Cells);
  //-Calculate initial cell of particles and check if there are unexpected excluded particles. | Calcula celda inicial de particulas y comprueba si hay excluidas inesperadas.
  LoadDcellParticles(Np,Codec,Posc,Dcellc);

  //-Create object for divide in CPU & select a valid cellmode. | Crea objeto para divide en Gpu y selecciona un cellmode valido.
  CellDivSingle=new JCellDivCpuSingle(Stable,FtCount!=0,PeriActive,CellOrder,CellMode,Scell,Map_PosMin,Map_PosMax,Map_Cells,CaseNbound,CaseNfixed,CaseNpb,Log,DirOut);
  CellDivSingle->DefineDomain(DomCellCode,DomCelIni,DomCelFin,DomPosMin,DomPosMax);
  ConfigCellDiv((JCellDivCpu*)CellDivSingle);

  ConfigSaveData(0,1,"");

  //-Reorder particles for cell. | Reordena particulas por celda.
  BoundChanged=true;
  RunCellDivide(true);
}

//==============================================================================
/// Redimension space reserved for particles in CPU, measure 
/// time consumed using TMC_SuResizeNp. On finishing, update divide.
///
/// Redimensiona el espacio reservado para particulas en CPU midiendo el
/// tiempo consumido con TMC_SuResizeNp. Al terminar actualiza el divide.
//==============================================================================
void JSphCpuSingle::ResizeParticlesSize(unsigned newsize,float oversize,bool updatedivide){
  TmcStart(Timers,TMC_SuResizeNp);
  newsize+=(oversize>0? unsigned(oversize*newsize): 0);
  ResizeCpuMemoryParticles(newsize);
  TmcStop(Timers,TMC_SuResizeNp);
  if(updatedivide)RunCellDivide(true);
}

//==============================================================================
/// Create list of new periodic particles to duplicate.
/// With stable activated reordered list of periodic particles.
///
/// Crea lista de nuevas particulas periodicas a duplicar.
/// Con stable activado reordena lista de periodicas.
//==============================================================================
unsigned JSphCpuSingle::PeriodicMakeList(unsigned n,unsigned pini,bool stable,unsigned nmax,tdouble3 perinc,const tdouble3 *pos,const typecode *code,unsigned *listp)const{
  unsigned count=0;
  if(n){
    //-Initialize size of list lsph to zero. | Inicializa tama�o de lista lspg a cero.
    listp[nmax]=0;
    for(unsigned p=0;p<n;p++){
      const unsigned p2=p+pini;
      //-Keep normal or periodic particles. | Se queda con particulas normales o periodicas.
      if(CODE_GetSpecialValue(code[p2])<=CODE_PERIODIC){
        //-Get particle position. | Obtiene posicion de particula.
        const tdouble3 ps=pos[p2];
        tdouble3 ps2=ps+perinc;
        if(Map_PosMin<=ps2 && ps2<Map_PosMax){
          unsigned cp=listp[nmax]; listp[nmax]++; if(cp<nmax)listp[cp]=p2;
        }
        ps2=ps-perinc;
        if(Map_PosMin<=ps2 && ps2<Map_PosMax){
          unsigned cp=listp[nmax]; listp[nmax]++; if(cp<nmax)listp[cp]=(p2|0x80000000);
        }
      }
    }
    count=listp[nmax];
    //-Reorder list if it is valid and stability is activated. | Reordena lista si es valida y stable esta activado.
    if(stable && count && count<=nmax){
      //-Don't make mistake because at the moment the list is not created using OpenMP. | No hace falta porque de momento no se crea la lista usando OpenMP.
    }
  }
  return(count);
}

//==============================================================================
/// Duplicate the indicated particle position applying displacement.
/// Duplicated particles are considered to be always valid and are inside
/// of the domain.
/// This kernel works for single-cpu & multi-cpu because the computations are done  
/// starting from domposmin.
/// It is controlled that the coordinates of the cell do not exceed the maximum.
///
/// Duplica la posicion de la particula indicada aplicandole un desplazamiento.
/// Las particulas duplicadas se considera que siempre son validas y estan dentro
/// del dominio.
/// Este kernel vale para single-cpu y multi-cpu porque los calculos se hacen 
/// a partir de domposmin.
/// Se controla que las coordendas de celda no sobrepasen el maximo.
//==============================================================================
void JSphCpuSingle::PeriodicDuplicatePos(unsigned pnew,unsigned pcopy,bool inverse,double dx,double dy,double dz,tuint3 cellmax,tdouble3 *pos,unsigned *dcell)const{
  //-Get pos of particle to be duplicated. | Obtiene pos de particula a duplicar.
  tdouble3 ps=pos[pcopy];
  //-Apply displacement. | Aplica desplazamiento.
  ps.x+=(inverse? -dx: dx);
  ps.y+=(inverse? -dy: dy);
  ps.z+=(inverse? -dz: dz);
  //-Calculate coordinates of cell inside of domain. | Calcula coordendas de celda dentro de dominio.
  unsigned cx=unsigned((ps.x-DomPosMin.x)/Scell);
  unsigned cy=unsigned((ps.y-DomPosMin.y)/Scell);
  unsigned cz=unsigned((ps.z-DomPosMin.z)/Scell);
  //-Adjust coordinates of cell is they exceed maximum. | Ajusta las coordendas de celda si sobrepasan el maximo.
  cx=(cx<=cellmax.x? cx: cellmax.x);
  cy=(cy<=cellmax.y? cy: cellmax.y);
  cz=(cz<=cellmax.z? cz: cellmax.z);
  //-Record position and cell of new particles. |  Graba posicion y celda de nuevas particulas.
  pos[pnew]=ps;
  dcell[pnew]=PC__Cell(DomCellCode,cx,cy,cz);
}

//==============================================================================
/// Create periodic particles starting from a list of the particles to duplicate.
/// Assume that all the particles are valid.
/// This kernel works for single-cpu & multi-cpu because it uses domposmin.
///
/// Crea particulas periodicas a partir de una lista con las particulas a duplicar.
/// Se presupone que todas las particulas son validas.
/// Este kernel vale para single-cpu y multi-cpu porque usa domposmin. 
//==============================================================================
void JSphCpuSingle::PeriodicDuplicateVerlet(unsigned np,unsigned pini,tuint3 cellmax,tdouble3 perinc,const unsigned *listp
  ,unsigned *idp,typecode *code,unsigned *dcell,tdouble3 *pos,tfloat4 *velrhop,tsymatrix3f *spstau,tfloat4 *velrhopm1)const
{
  const int n=int(np);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
  #endif
  for(int p=0;p<n;p++){
    const unsigned pnew=unsigned(p)+pini;
    const unsigned rp=listp[p];
    const unsigned pcopy=(rp&0x7FFFFFFF);
    //-Adjust position and cell of new particle. | Ajusta posicion y celda de nueva particula.
    PeriodicDuplicatePos(pnew,pcopy,(rp>=0x80000000),perinc.x,perinc.y,perinc.z,cellmax,pos,dcell);
    //-Copy the rest of the values. | Copia el resto de datos.
    idp[pnew]=idp[pcopy];
    code[pnew]=CODE_SetPeriodic(code[pcopy]);
    velrhop[pnew]=velrhop[pcopy];
    velrhopm1[pnew]=velrhopm1[pcopy];
    if(spstau)spstau[pnew]=spstau[pcopy];
  }
}

//==============================================================================
/// Create periodic particles starting from a list of the particles to duplicate.
/// Assume that all the particles are valid.
/// This kernel works for single-cpu & multi-cpu because it uses domposmin.
///
/// Crea particulas periodicas a partir de una lista con las particulas a duplicar.
/// Se presupone que todas las particulas son validas.
/// Este kernel vale para single-cpu y multi-cpu porque usa domposmin. 
//==============================================================================
void JSphCpuSingle::PeriodicDuplicateSymplectic(unsigned np,unsigned pini,tuint3 cellmax,tdouble3 perinc,const unsigned *listp
  ,unsigned *idp,typecode *code,unsigned *dcell,tdouble3 *pos,tfloat4 *velrhop,tsymatrix3f *spstau,tdouble3 *pospre,tfloat4 *velrhoppre)const
{
  const int n=int(np);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
  #endif
  for(int p=0;p<n;p++){
    const unsigned pnew=unsigned(p)+pini;
    const unsigned rp=listp[p];
    const unsigned pcopy=(rp&0x7FFFFFFF);
    //-Adjust position and cell of new particle. | Ajusta posicion y celda de nueva particula.
    PeriodicDuplicatePos(pnew,pcopy,(rp>=0x80000000),perinc.x,perinc.y,perinc.z,cellmax,pos,dcell);
    //-Copy the rest of the values. | Copia el resto de datos.
    idp[pnew]=idp[pcopy];
    code[pnew]=CODE_SetPeriodic(code[pcopy]);
    velrhop[pnew]=velrhop[pcopy];
    if(pospre)pospre[pnew]=pospre[pcopy];
    if(velrhoppre)velrhoppre[pnew]=velrhoppre[pcopy];
    if(spstau)spstau[pnew]=spstau[pcopy];
  }
}

//==============================================================================
/// Create duplicate particles for periodic conditions.
/// Create new periodic particles and mark the old ones to be ignored.
/// New periodic particles are created from Np of the beginning, first the NpbPer
/// of the boundry and then the NpfPer fluid ones. The Np of the those leaving contains also the
/// new periodic ones.
///
/// Crea particulas duplicadas de condiciones periodicas.
/// Crea nuevas particulas periodicas y marca las viejas para ignorarlas.
/// Las nuevas periodicas se situan a partir del Np de entrada, primero las NpbPer
/// de contorno y despues las NpfPer fluidas. El Np de salida contiene tambien las
/// nuevas periodicas.
//==============================================================================
void JSphCpuSingle::RunPeriodic(){
  const char met[]="RunPeriodic";
  TmcStart(Timers,TMC_SuPeriodic);
  //-Keep number of present periodic. | Guarda numero de periodicas actuales.
  NpfPerM1=NpfPer;
  NpbPerM1=NpbPer;
  //-Mark present periodic particles to ignore. | Marca periodicas actuales para ignorar.
  for(unsigned p=0;p<Np;p++){
    const typecode rcode=Codec[p];
    if(CODE_IsPeriodic(rcode))Codec[p]=CODE_SetOutIgnore(rcode);
  }
  //-Create new periodic particles. | Crea las nuevas periodicas.
  const unsigned npb0=Npb;
  const unsigned npf0=Np-Npb;
  const unsigned np0=Np;
  NpbPer=NpfPer=0;
  BoundChanged=true;
  for(unsigned ctype=0;ctype<2;ctype++){//-0:bound, 1:fluid+floating.
    //-Calculate range of particles to be examined (bound or fluid). | Calcula rango de particulas a examinar (bound o fluid).
    const unsigned pini=(ctype? npb0: 0);
    const unsigned num= (ctype? npf0: npb0);
    //-Search for periodic in each direction (X, Y, or Z). | Busca periodicas en cada eje (X, Y e Z).
    for(unsigned cper=0;cper<3;cper++)if((cper==0 && PeriActive&1) || (cper==1 && PeriActive&2) || (cper==2 && PeriActive&4)){
      tdouble3 perinc=(cper==0? PeriXinc: (cper==1? PeriYinc: PeriZinc));
      //-First search in the list of new periodic particles and then in the initial list of particles (this is needed for periodic particles in more than one direction).
      //-Primero busca en la lista de periodicas nuevas y despues en la lista inicial de particulas (necesario para periodicas en mas de un eje).
      for(unsigned cblock=0;cblock<2;cblock++){//-0:new periodic, 1:original particles. | 0:periodicas nuevas, 1:particulas originales
        const unsigned nper=(ctype? NpfPer: NpbPer); //-Number of new periodic particles of type to be processed. | Numero de periodicas nuevas del tipo a procesar.
        const unsigned pini2=(cblock? pini: Np-nper);
        const unsigned num2= (cblock? num:  nper);
        //-Repeat the search if the resulting memory available is insufficient and it had to be increased.
        //-Repite la busqueda si la memoria disponible resulto insuficiente y hubo que aumentarla.
        bool run=true;
        while(run && num2){
          //-Reserve memory to create list of periodic particles. | Reserva memoria para crear lista de particulas periodicas.
          unsigned* listp=ArraysCpu->ReserveUint();
          unsigned nmax=CpuParticlesSize-1; //-Maximmum number of particles that fit in the list. | Numero maximo de particulas que caben en la lista.
          //-Generate list of new periodic particles. | Genera lista de nuevas periodicas.
          if(Np>=0x80000000)RunException(met,"The number of particles is too big.");//-Because the last bit is used to mark the direction in which a new periodic particle is created. | Porque el ultimo bit se usa para marcar el sentido en que se crea la nueva periodica.
          unsigned count=PeriodicMakeList(num2,pini2,Stable,nmax,perinc,Posc,Codec,listp);
          //-Redimension memory for particles if there is insufficient space and repeat the search process.
          //-Redimensiona memoria para particulas si no hay espacio suficiente y repite el proceso de busqueda.
          if(count>nmax || !CheckCpuParticlesSize(count+Np)){
            ArraysCpu->Free(listp); listp=NULL;
            TmcStop(Timers,TMC_SuPeriodic);
            ResizeParticlesSize(Np+count,PERIODIC_OVERMEMORYNP,false);
            TmcStart(Timers,TMC_SuPeriodic);
          }
          else{
            run=false;
            //-Create new duplicate periodic particles in the list
            //-Crea nuevas particulas periodicas duplicando las particulas de la lista.
            if(TStep==STEP_Verlet)PeriodicDuplicateVerlet(count,Np,DomCells,perinc,listp,Idpc,Codec,Dcellc,Posc,Velrhopc,SpsTauc,VelrhopM1c);
            if(TStep==STEP_Symplectic){
              if((PosPrec || VelrhopPrec) && (!PosPrec || !VelrhopPrec))RunException(met,"Symplectic data is invalid.") ;
              PeriodicDuplicateSymplectic(count,Np,DomCells,perinc,listp,Idpc,Codec,Dcellc,Posc,Velrhopc,SpsTauc,PosPrec,VelrhopPrec);
            }

            //-Free the list and update the number of particles. | Libera lista y actualiza numero de particulas.
            ArraysCpu->Free(listp); listp=NULL;
            Np+=count;
            //-Update number of new periodic particles. | Actualiza numero de periodicas nuevas.
            if(!ctype)NpbPer+=count;
            else NpfPer+=count;
          }
        }
      }
    }
  }
  TmcStop(Timers,TMC_SuPeriodic);
}

//==============================================================================
/// Execute divide of particles in cells.
/// Ejecuta divide de particulas en celdas.
//==============================================================================
void JSphCpuSingle::RunCellDivide(bool updateperiodic){
  const char met[]="RunCellDivide";
  //-Create new periodic particles & mark the old ones to be ignored. | Crea nuevas particulas periodicas y marca las viejas para ignorarlas.
  if(updateperiodic && PeriActive)RunPeriodic();

  //-Initialises Divide. | Inicia Divide.
  CellDivSingle->Divide(Npb,Np-Npb-NpbPer-NpfPer,NpbPer,NpfPer,BoundChanged,Dcellc,Codec,Idpc,Posc,Timers);

  //-Order particle data. | Ordena datos de particulas
  TmcStart(Timers,TMC_NlSortData);
  CellDivSingle->SortArray(Idpc);
  CellDivSingle->SortArray(Codec);
  CellDivSingle->SortArray(Dcellc);
  CellDivSingle->SortArray(Posc);
  CellDivSingle->SortArray(Velrhopc);
  if(TStep==STEP_Verlet){
    CellDivSingle->SortArray(VelrhopM1c);
  }
  else if(TStep==STEP_Symplectic && (PosPrec || VelrhopPrec)){//-In reality, this is only necessary in divide for corrector, not in predictor??? | En realidad solo es necesario en el divide del corrector, no en el predictor???
    if(!PosPrec || !VelrhopPrec)RunException(met,"Symplectic data is invalid.") ;
    CellDivSingle->SortArray(PosPrec);
    CellDivSingle->SortArray(VelrhopPrec);
  }
  if(TVisco==VISCO_LaminarSPS)CellDivSingle->SortArray(SpsTauc);

  //-Collect divide data. | Recupera datos del divide.
  Np=CellDivSingle->GetNpFinal();
  Npb=CellDivSingle->GetNpbFinal();
  NpbOk=Npb-CellDivSingle->GetNpbIgnore();
  //-Collect position of floating particles. | Recupera posiciones de floatings.
  if(CaseNfloat)CalcRidp(PeriActive!=0,Np-Npb,Npb,CaseNpb,CaseNpb+CaseNfloat,Codec,Idpc,FtRidp);
  TmcStop(Timers,TMC_NlSortData);

  //-Control of excluded particles (only fluid because if some bound is excluded ti generates an exception in Divide()).
  //-Gestion de particulas excluidas (solo fluid pq si alguna bound es excluida se genera excepcion en Divide()).
  TmcStart(Timers,TMC_NlOutCheck);
  unsigned npfout=CellDivSingle->GetNpOut();
  if(npfout){
    unsigned* idp=ArraysCpu->ReserveUint();
    tdouble3* pos=ArraysCpu->ReserveDouble3();
    tfloat3* vel=ArraysCpu->ReserveFloat3();
    float* rhop=ArraysCpu->ReserveFloat();
    unsigned num=GetParticlesData(npfout,Np,true,false,idp,pos,vel,rhop,NULL);
    AddParticlesOut(npfout,idp,pos,vel,rhop,CellDivSingle->GetNpfOutRhop(),CellDivSingle->GetNpfOutMove());
    ArraysCpu->Free(idp);
    ArraysCpu->Free(pos);
    ArraysCpu->Free(vel);
    ArraysCpu->Free(rhop);
  }
  TmcStop(Timers,TMC_NlOutCheck);
  BoundChanged=false;
}

//------------------------------------------------------------------------------
/// Returns cell limits for interaction.
/// Devuelve limites de celdas para interaccion.
//------------------------------------------------------------------------------
void JSphCpuSingle::GetInteractionCells(unsigned rcell
  ,int hdiv,const tint4 &nc,const tint3 &cellzero
  ,int &cxini,int &cxfin,int &yini,int &yfin,int &zini,int &zfin)const
{
  //-Get interaction limits. | Obtiene limites de interaccion
  const int cx=PC__Cellx(DomCellCode,rcell)-cellzero.x;
  const int cy=PC__Celly(DomCellCode,rcell)-cellzero.y;
  const int cz=PC__Cellz(DomCellCode,rcell)-cellzero.z;
  //-Code for hdiv 1 or 2 but not zero. | Codigo para hdiv 1 o 2 pero no cero.
  cxini=cx-min(cx,hdiv);
  cxfin=cx+min(nc.x-cx-1,hdiv)+1;
  yini=cy-min(cy,hdiv);
  yfin=cy+min(nc.y-cy-1,hdiv)+1;
  zini=cz-min(cz,hdiv);
  zfin=cz+min(nc.z-cz-1,hdiv)+1;
}

//==============================================================================
/// Interaction to calculate forces.
/// Interaccion para el calculo de fuerzas.
//==============================================================================
void JSphCpuSingle::Interaction_Forces(TpInter tinter){
  const char met[]="Interaction_Forces";
  PreInteraction_Forces(tinter);
  TmcStart(Timers,TMC_CfForces);

  //-Interaction of Fluid-Fluid/Bound & Bound-Fluid (forces and DEM). | Interaccion Fluid-Fluid/Bound & Bound-Fluid (forces and DEM).
  float viscdt=0;
  if(Psingle)JSphCpu::InteractionSimple_Forces(Np,Npb,NpbOk,CellDivSingle->GetNcells(),CellDivSingle->GetBeginCell(),CellDivSingle->GetCellDomainMin(),Dcellc,PsPosc,Velrhopc,Idpc,Codec,Pressc,viscdt,Arc,Acec,Deltac,SpsTauc,SpsGradvelc,ShiftPosc,ShiftDetectc);
  else JSphCpu::Interaction_Forces(Np,Npb,NpbOk,CellDivSingle->GetNcells(),CellDivSingle->GetBeginCell(),CellDivSingle->GetCellDomainMin(),Dcellc,Posc,Velrhopc,Idpc,Codec,Pressc,viscdt,Arc,Acec,Deltac,SpsTauc,SpsGradvelc,ShiftPosc,ShiftDetectc);

  //-For 2-D simulations zero the 2nd component. | Para simulaciones 2D anula siempre la 2� componente.
  if(Simulate2D){
    const int ini=int(Npb),fin=int(Np),npf=int(Np-Npb);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
    #endif
    for(int p=ini;p<fin;p++)Acec[p].y=0;
  }

  //-Add Delta-SPH correction to Arg[]. | A�ade correccion de Delta-SPH a Arg[].
  if(Deltac){
    const int ini=int(Npb),fin=int(Np),npf=int(Np-Npb);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
    #endif
    for(int p=ini;p<fin;p++)if(Deltac[p]!=FLT_MAX)Arc[p]+=Deltac[p];
  }

  //-Calculates maximum value of ViscDt.
  ViscDtMax=viscdt;
  //-Calculates maximum value of Ace.
  if(PeriActive!=0)AceMax=ComputeAceMaxOmp<true> (Np-Npb,Acec+Npb,Codec+Npb);
  else             AceMax=ComputeAceMaxOmp<false>(Np-Npb,Acec+Npb,Codec+Npb);

  TmcStop(Timers,TMC_CfForces);
}

//==============================================================================
/// Returns maximum value of ace (modulus).
/// Devuelve el valor maximo de ace (modulo).
//==============================================================================
template<bool checkcodenormal> double JSphCpuSingle::ComputeAceMaxSeq(unsigned np,const tfloat3* ace,const typecode *code)const{
  float acemax=0;
  const int n=int(np);
  for(int p=0;p<n;p++){
    if(!checkcodenormal){
      const tfloat3 a=ace[p];
      const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
      acemax=max(acemax,a2);
    }
    //-With periodic conditions ignore periodic particles. | Con condiciones periodicas ignora las particulas periodicas.
    else if(CODE_IsNormal(code[p])){
      const tfloat3 a=ace[p];
      const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
      acemax=max(acemax,a2);
    }
  }
  return(sqrt(double(acemax)));
}

//==============================================================================
/// Returns maximum value of ace (modulus) using OpenMP.
/// Devuelve el valor maximo de ace (modulo) using OpenMP.
//==============================================================================
template<bool checkcodenormal> double JSphCpuSingle::ComputeAceMaxOmp(unsigned np,const tfloat3* ace,const typecode *code)const{
  const char met[]="ComputeAceMaxOmp";
  double acemax=0;
  #ifdef OMP_USE
    if(np>OMP_LIMIT_COMPUTELIGHT){
      const int n=int(np);
      if(n<0)RunException(met,"Number of values is too big.");
      float amax=0;
      #pragma omp parallel 
      {
        float amax2=0;
        #pragma omp for nowait
        for(int p=0;p<n;++p){
          if(!checkcodenormal){
            const tfloat3 a=ace[p];
            const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
            if(amax2<a2)amax2=a2;
          }
          //-With periodic conditions ignore periodic particles. | Con condiciones periodicas ignora las particulas periodicas.
          else if(CODE_IsNormal(code[p])){
            const tfloat3 a=ace[p];
            const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
            if(amax2<a2)amax2=a2;
          }
        }
        #pragma omp critical 
        {
          if(amax<amax2)amax=amax2;
        }
      }
      //-Saves result.
      acemax=sqrt(double(amax));
    }
    else if(np)acemax=ComputeAceMaxSeq<checkcodenormal>(np,ace,code);
  #else
    if(np)acemax=ComputeAceMaxSeq<checkcodenormal>(np,ace,code);
  #endif
  return(acemax);
}

//==============================================================================
/// Perform interactions and updates of particles according to forces 
/// calculated in the interaction using Verlet.
///
/// Realiza interaccion y actualizacion de particulas segun las fuerzas 
/// calculadas en la interaccion usando Verlet.
//==============================================================================
double JSphCpuSingle::ComputeStep_Ver(){
  Interaction_Forces(INTER_Forces);    //-Interaction.
  const double dt=DtVariable(true);    //-Calculate new dt.
  DemDtForce=dt;                       //(DEM)
  if(TShifting)RunShifting(dt);        //-Shifting.
  ComputeVerlet(dt);                   //-Update particles using Verlet.
  if(CaseNfloat)RunFloating(dt,false); //-Control of floating bodies.
  PosInteraction_Forces();             //-Free memory used for interaction.
  if(Damping)RunDamping(dt,Np,Npb,Posc,Codec,Velrhopc); //-Applies Damping.
  return(dt);
}

//==============================================================================
/// Perform interactions and updates of particles according to forces 
/// calculated in the interaction using Symplectic.
///
/// Realiza interaccion y actualizacion de particulas segun las fuerzas 
/// calculadas en la interaccion usando Symplectic.
//==============================================================================
double JSphCpuSingle::ComputeStep_Sym(){
  const double dt=DtPre;
  //-Predictor
  //-----------
  DemDtForce=dt*0.5f;                     //(DEM)
  Interaction_Forces(INTER_Forces);       //-Interaction.
  const double ddt_p=DtVariable(false);   //-Calculate dt of predictor step.
  if(TShifting)RunShifting(dt*.5);        //-Shifting.
  ComputeSymplecticPre(dt);               //-Apply Symplectic-Predictor to particles.
  if(CaseNfloat)RunFloating(dt*.5,true);  //-Control of floating bodies.
  PosInteraction_Forces();                //-Free memory used for interaction.
  //-Corrector
  //-----------
  DemDtForce=dt;                          //(DEM)
  RunCellDivide(true);
  Interaction_Forces(INTER_ForcesCorr);   //Interaction.
  const double ddt_c=DtVariable(true);    //-Calculate dt of corrector step.
  if(TShifting)RunShifting(dt);           //-Shifting.
  ComputeSymplecticCorr(dt);              //-Apply Symplectic-Corrector to particles.
  if(CaseNfloat)RunFloating(dt,false);    //-Control of floating bodies.
  PosInteraction_Forces();                //-Free memory used for interaction.
  if(Damping)RunDamping(dt,Np,Npb,Posc,Codec,Velrhopc); //-Applies Damping.

  DtPre=min(ddt_p,ddt_c);                 //-Calculate dt for next ComputeStep.
  return(dt);
}

//==============================================================================
/// Calculate distance between floating particles & centre according to periodic conditions.
/// Calcula distancia entre pariculas floatin y centro segun condiciones periodicas.
//==============================================================================
tfloat3 JSphCpuSingle::FtPeriodicDist(const tdouble3 &pos,const tdouble3 &center,float radius)const{
  tdouble3 distd=(pos-center);
  if(PeriX && fabs(distd.x)>radius){
    if(distd.x>0)distd=distd+PeriXinc;
    else distd=distd-PeriXinc;
  }
  if(PeriY && fabs(distd.y)>radius){
    if(distd.y>0)distd=distd+PeriYinc;
    else distd=distd-PeriYinc;
  }
  if(PeriZ && fabs(distd.z)>radius){
    if(distd.z>0)distd=distd+PeriZinc;
    else distd=distd-PeriZinc;
  }
  return(ToTFloat3(distd));
}

//==============================================================================
/// Calculate summation: face, fomegaace.
/// Calcula suma de face y fomegaace a partir de particulas floating.
//==============================================================================
void JSphCpuSingle::FtCalcForcesSum(unsigned cf,tfloat3 &face,tfloat3 &fomegaace)const{
  const StFloatingData fobj=FtObjs[cf];
  const unsigned fpini=fobj.begin-CaseNpb;
  const unsigned fpfin=fpini+fobj.count;
  const float fradius=fobj.radius;
  const tdouble3 fcenter=fobj.center;
  //-Computes traslational and rotational velocities.
  face=TFloat3(0);
  fomegaace=TFloat3(0);
  //-Calculate summation: face, fomegaace. | Calcula sumatorios: face, fomegaace.
  for(unsigned fp=fpini;fp<fpfin;fp++){
    int p=FtRidp[fp];
    //-Ace is initialised with the value of the gravity for all particles.
    float acex=Acec[p].x-Gravity.x,acey=Acec[p].y-Gravity.y,acez=Acec[p].z-Gravity.z;
    face.x+=acex; face.y+=acey; face.z+=acez;
    tfloat3 dist=(PeriActive? FtPeriodicDist(Posc[p],fcenter,fradius): ToTFloat3(Posc[p]-fcenter)); 
    fomegaace.x+= acez*dist.y - acey*dist.z;
    fomegaace.y+= acex*dist.z - acez*dist.x;
    fomegaace.z+= acey*dist.x - acex*dist.y;
  }
}

//==============================================================================
/// Calculate forces around floating object particles.
/// Calcula fuerzas sobre floatings.
//==============================================================================
void JSphCpuSingle::FtCalcForces(StFtoForces *ftoforces)const{
  const int ftcount=int(FtCount);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (guided)
  #endif
  for(int cf=0;cf<ftcount;cf++){
    const StFloatingData fobj=FtObjs[cf];
    const float fmass=fobj.mass;
    const tfloat3 fang=fobj.angles;
    tmatrix3f inert=fobj.inertiaini;

    //-Compute a cumulative rotation matrix.
    const tmatrix3f frot=fmath::RotMatrix3x3(fang);
    //-Compute the intertia tensor by rotating the initial tensor to the curent orientation I=(R*I_0)*R^T.
    inert=fmath::MulMatrix3x3(fmath::MulMatrix3x3(frot,inert),fmath::TrasMatrix3x3(frot));
    //-Calculates the inverse of the intertia matrix to compute the I^-1 * L= W
    const tmatrix3f invinert=fmath::InverseMatrix3x3(inert);

    //-Computes traslational and rotational velocities.
    tfloat3 face,fomegaace;
    FtCalcForcesSum(cf,face,fomegaace);

    //-Calculate omega starting from fomegaace & invinert. | Calcula omega a partir de fomegaace y invinert.
    {
      tfloat3 omegaace;
      omegaace.x=(fomegaace.x*invinert.a11+fomegaace.y*invinert.a12+fomegaace.z*invinert.a13);
      omegaace.y=(fomegaace.x*invinert.a21+fomegaace.y*invinert.a22+fomegaace.z*invinert.a23);
      omegaace.z=(fomegaace.x*invinert.a31+fomegaace.y*invinert.a32+fomegaace.z*invinert.a33);
      fomegaace=omegaace;
    }
    //-Add gravity and divide by mass. | A�ade gravedad y divide por la masa.
    face.x=(face.x+fmass*Gravity.x)/fmass;
    face.y=(face.y+fmass*Gravity.y)/fmass;
    face.z=(face.z+fmass*Gravity.z)/fmass;
    //-Keep result in ftoforces[]. | Guarda resultados en ftoforces[].
    ftoforces[cf].face=ftoforces[cf].face+face;
    ftoforces[cf].fomegaace=ftoforces[cf].fomegaace+fomegaace;
  }
}

//==============================================================================
/// Calculate data to update floatings.
/// Calcula datos para actualizar floatings.
//==============================================================================
void JSphCpuSingle::FtCalcForcesRes(double dt,const StFtoForces *ftoforces,StFtoForcesRes *ftoforcesres)const{
  for(unsigned cf=0;cf<FtCount;cf++){
    //-Get Floating object values. | Obtiene datos de floating.
    const StFloatingData fobj=FtObjs[cf];
    //-Compute fomega. | Calculo de fomega.
    tfloat3 fomega=fobj.fomega;
    {
      const tfloat3 omegaace=FtoForces[cf].fomegaace;
      fomega.x=float(dt*omegaace.x+fomega.x);
      fomega.y=float(dt*omegaace.y+fomega.y);
      fomega.z=float(dt*omegaace.z+fomega.z);
    }
    tfloat3 fvel=fobj.fvel;
    //-Zero components for 2-D simulation. | Anula componentes para 2D.
    tfloat3 face=FtoForces[cf].face;
    if(Simulate2D){ face.y=0; fomega.x=0; fomega.z=0; fvel.y=0; }
    //-Compute fcenter. | Calculo de fcenter.
    tdouble3 fcenter=fobj.center;
    fcenter.x+=dt*fvel.x;
    fcenter.y+=dt*fvel.y;
    fcenter.z+=dt*fvel.z;
    //-Compute fvel. | Calculo de fvel.
    fvel.x=float(dt*face.x+fvel.x);
    fvel.y=float(dt*face.y+fvel.y);
    fvel.z=float(dt*face.z+fvel.z);
    //-Store data to update floating. | Guarda datos para actualizar floatings.
    FtoForcesRes[cf].fomegares=fomega;
    FtoForcesRes[cf].fvelres=fvel;
    FtoForcesRes[cf].fcenterres=fcenter;
  }
}

//==============================================================================
/// Process floating objects
/// Procesa floating objects.
//==============================================================================
void JSphCpuSingle::RunFloating(double dt,bool predictor){
  const char met[]="RunFloating";
  if(TimeStep>=FtPause){//-Operator >= is used because when FtPause=0 in symplectic-predictor, code would not enter here. | Se usa >= pq si FtPause es cero en symplectic-predictor no entraria.
    TmcStart(Timers,TMC_SuFloating);
    //-Initialises forces of floatings.
    memset(FtoForces,0,sizeof(StFtoForces)*FtCount); 

    //-Adds calculated forces around floating objects. | A�ade fuerzas calculadas sobre floatings.
    FtCalcForces(FtoForces);
    //-Calculate data to update floatings. | Calcula datos para actualizar floatings.
    FtCalcForcesRes(dt,FtoForces,FtoForcesRes);

    //-Apply movement around floating objects. | Aplica movimiento sobre floatings.
    const int ftcount=int(FtCount);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (guided)
    #endif
    for(int cf=0;cf<ftcount;cf++){
      //-Get Floating object values.
      const StFloatingData fobj=FtObjs[cf];
      const tfloat3 fomega=FtoForcesRes[cf].fomegares;
      const tfloat3 fvel=FtoForcesRes[cf].fvelres;
      const tdouble3 fcenter=FtoForcesRes[cf].fcenterres;
      //-Updates floating particles.
      const float fradius=fobj.radius;
      const unsigned fpini=fobj.begin-CaseNpb;
      const unsigned fpfin=fpini+fobj.count;
      for(unsigned fp=fpini;fp<fpfin;fp++){
        const int p=FtRidp[fp];
        if(p!=UINT_MAX){
          tfloat4 *velrhop=Velrhopc+p;
          //-Compute and record position displacement. | Calcula y graba desplazamiento de posicion.
          const double dx=dt*double(velrhop->x);
          const double dy=dt*double(velrhop->y);
          const double dz=dt*double(velrhop->z);
          UpdatePos(Posc[p],dx,dy,dz,false,p,Posc,Dcellc,Codec);
          //-Compute and record new velocity. | Calcula y graba nueva velocidad.
          tfloat3 dist=(PeriActive? FtPeriodicDist(Posc[p],fcenter,fradius): ToTFloat3(Posc[p]-fcenter)); 
          velrhop->x=fvel.x+(fomega.y*dist.z-fomega.z*dist.y);
          velrhop->y=fvel.y+(fomega.z*dist.x-fomega.x*dist.z);
          velrhop->z=fvel.z+(fomega.x*dist.y-fomega.y*dist.x);
        }
      }

      //-Stores floating data.
      if(!predictor){
        //const tdouble3 centerold=FtObjs[cf].center;
        FtObjs[cf].center=(PeriActive? UpdatePeriodicPos(fcenter): fcenter);
        FtObjs[cf].angles=ToTFloat3(ToTDouble3(FtObjs[cf].angles)+ToTDouble3(fomega)*dt);
        FtObjs[cf].fvel=fvel;
        FtObjs[cf].fomega=fomega;
      }
    }
    TmcStop(Timers,TMC_SuFloating);
  }
}

//==============================================================================
/// Runs calculations in configured gauges.
/// Ejecuta calculos en las posiciones de medida configuradas.
//==============================================================================
void JSphCpuSingle::RunGaugeSystem(double timestep){
  const bool svpart=(TimeStep>=TimePartNext);
  GaugeSystem->CalculeCpu(timestep,svpart,CellDivSingle->GetNcells(),CellDivSingle->GetCellDomainMin(),CellDivSingle->GetBeginCell(),Posc,Codec,Velrhopc);
}

//==============================================================================
/// Initialises execution of simulation.
/// Inicia ejecucion de simulacion.
//==============================================================================
void JSphCpuSingle::Run(std::string appname,JCfgRun *cfg,JLog2 *log){
  const char* met="Run";
  if(!cfg||!log)return;
  AppName=appname; Log=log;

  //-Configure timers.
  //-------------------
  TmcCreation(Timers,cfg->SvTimers);
  TmcStart(Timers,TMC_Init);

  //-Load parameters and values of input. | Carga de parametros y datos de entrada.
  //--------------------------------------------------------------------------------
  LoadConfig(cfg);
  LoadCaseParticles();
  ConfigConstants(Simulate2D);
  ConfigDomain();
  ConfigRunMode(cfg);
  VisuParticleSummary();

  //-Initialisation of execution variables. | Inicializacion de variables de ejecucion.
  //------------------------------------------------------------------------------------
  InitRun();
  RunGaugeSystem(TimeStep);
  UpdateMaxValues();
  PrintAllocMemory(GetAllocMemoryCpu());
  SaveData(); 
  TmcResetValues(Timers);
  TmcStop(Timers,TMC_Init);
  PartNstep=-1; Part++;

  //-Main Loop.
  //------------
  JTimeControl tc("30,60,300,600");//-Shows information at 0.5, 1, 5 y 10 minutes (before first PART).
  bool partoutstop=false;
  TimerSim.Start();
  TimerPart.Start();
  Log->Print(string("\n[Initialising simulation (")+RunCode+")  "+fun::GetDateTime()+"]");
  PrintHeadPart();
  while(TimeStep<TimeMax){
    if(ViscoTime)Visco=ViscoTime->GetVisco(float(TimeStep));
    double stepdt=ComputeStep();
    RunGaugeSystem(TimeStep+stepdt);
    if(PartDtMin>stepdt)PartDtMin=stepdt; if(PartDtMax<stepdt)PartDtMax=stepdt;
    if(CaseNmoving)RunMotion(stepdt);
    RunCellDivide(true);
    TimeStep+=stepdt;
    partoutstop=(Np<NpMinimum || !Np);
    if(TimeStep>=TimePartNext || partoutstop){
      if(partoutstop){
        Log->Print("\n**** Particles OUT limit reached...\n");
        TimeMax=TimeStep;
      }
      SaveData();
      Part++;
      PartNstep=Nstep;
      TimeStepM1=TimeStep;
      TimePartNext=TimeOut->GetNextTime(TimeStep);
      TimerPart.Start();
    }
    UpdateMaxValues();
    Nstep++;
    if(Part<=PartIni+1 && tc.CheckTime())Log->Print(string("  ")+tc.GetInfoFinish((TimeStep-TimeStepIni)/(TimeMax-TimeStepIni)));
    //if(Nstep>=3)break;
  }
  TimerSim.Stop(); TimerTot.Stop();

  //-End of Simulation.
  //--------------------
  FinishRun(partoutstop);
}

//==============================================================================
/// Generates files with output data.
/// Genera los ficheros de salida de datos.
//==============================================================================
void JSphCpuSingle::SaveData(){
  const bool save=(SvData!=SDAT_None&&SvData!=SDAT_Info);
  const unsigned npsave=Np-NpbPer-NpfPer; //-Subtracts the periodic particles if they exist. | Resta las periodicas si las hubiera.
  TmcStart(Timers,TMC_SuSavePart);
  //-Collect particle values in original order. | Recupera datos de particulas en orden original.
  unsigned *idp=NULL;
  tdouble3 *pos=NULL;
  tfloat3 *vel=NULL;
  float *rhop=NULL;
  if(save){
    //-Assign memory and collect particle values. | Asigna memoria y recupera datos de las particulas.
    idp=ArraysCpu->ReserveUint();
    pos=ArraysCpu->ReserveDouble3();
    vel=ArraysCpu->ReserveFloat3();
    rhop=ArraysCpu->ReserveFloat();
    unsigned npnormal=GetParticlesData(Np,0,true,PeriActive!=0,idp,pos,vel,rhop,NULL);
    if(npnormal!=npsave)RunException("SaveData","The number of particles is invalid.");
  }
  //-Gather additional information. | Reune informacion adicional.
  StInfoPartPlus infoplus;
  memset(&infoplus,0,sizeof(StInfoPartPlus));
  if(SvData&SDAT_Info){
    infoplus.nct=CellDivSingle->GetNct();
    infoplus.npbin=NpbOk;
    infoplus.npbout=Npb-NpbOk;
    infoplus.npf=Np-Npb;
    infoplus.npbper=NpbPer;
    infoplus.npfper=NpfPer;
    infoplus.memorycpualloc=this->GetAllocMemoryCpu();
    infoplus.gpudata=false;
    TimerSim.Stop();
    infoplus.timesim=TimerSim.GetElapsedTimeD()/1000.;
  }
  //-Stores particle data. | Graba datos de particulas.
  const tdouble3 vdom[2]={OrderDecode(CellDivSingle->GetDomainLimits(true)),OrderDecode(CellDivSingle->GetDomainLimits(false))};
  JSph::SaveData(npsave,idp,pos,vel,rhop,1,vdom,&infoplus);
  //-Free auxiliary memory for particle data. | Libera memoria auxiliar para datos de particulas.
  ArraysCpu->Free(idp);
  ArraysCpu->Free(pos);
  ArraysCpu->Free(vel);
  ArraysCpu->Free(rhop);
  TmcStop(Timers,TMC_SuSavePart);
}

//==============================================================================
/// Displays and stores final summary of the execution.
/// Muestra y graba resumen final de ejecucion.
//==============================================================================
void JSphCpuSingle::FinishRun(bool stop){
  float tsim=TimerSim.GetElapsedTimeF()/1000.f,ttot=TimerTot.GetElapsedTimeF()/1000.f;
  JSph::ShowResume(stop,tsim,ttot,true,"");
  string hinfo=";RunMode",dinfo=string(";")+RunMode;
  if(SvTimers){
    ShowTimers();
    GetTimersInfo(hinfo,dinfo);
  }
  Log->Print(" ");
  if(SvRes)SaveRes(tsim,ttot,hinfo,dinfo);
}


