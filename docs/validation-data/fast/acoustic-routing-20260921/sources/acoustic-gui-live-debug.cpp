#include "application.hpp"
#include "datapump/types.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
using namespace datapump::gui;
using namespace std::chrono_literals;
int main(int argc,char**argv){
 try {
  if(argc!=3)throw std::runtime_error("usage: gui-audio-debug tx|rx save-path");
  const bool tx=std::string(argv[1])=="tx";
  Application app(Launch{});app.start();
  app.select(ui::Field::fast_mode,"fast");app.select(ui::Field::fast_profile,"acoustic");
  const std::string message="Acoustic GUI diagnosis: exact received text with real audio.";
  app.edit(ui::Field::fast_text,message);
  std::cout<<"profile="<<app.field(ui::Field::fast_profile).selected<<" constellation="<<app.field(ui::Field::fast_constellation).selected<<" code="<<app.field(ui::Field::fast_coding).selected<<" depth="<<app.field(ui::Field::fast_depth).selected<<" mono="<<app.field(ui::Field::fast_mono).checked<<std::endl;
  app.activate(tx?ui::Command::fast_transmit:ui::Command::fast_listen);
  auto begin=std::chrono::steady_clock::now(),next=begin;
  bool timeout=false;
  while(true){
   app.tick();auto now=std::chrono::steady_clock::now();
   if(now>=next){
    std::cout<<std::chrono::duration<double>(now-begin).count()<<" | "<<app.field(ui::Field::fast_status).text<<" | "<<app.field(ui::Field::fast_progress).text<<" | "<<app.field(ui::Field::fast_tracking).text<<" | "<<app.field(ui::Field::fast_auth).text<<std::endl;
    next=now+1s;
   }
   if(!app.enabled(ui::Command::fast_cancel))break;
   if(now-begin>55s){app.activate(ui::Command::fast_cancel);timeout=true;}
   std::this_thread::sleep_for(20ms);
  }
  std::this_thread::sleep_for(100ms);app.tick();
  bool passed=!timeout;
  if(!tx){
   passed=passed&&app.enabled(ui::Command::fast_save);
   if(passed){
    app.activate(ui::Command::fast_save);auto requests=app.take_services();
    if(requests.size()!=1)throw std::runtime_error("missing Save service");
    app.complete_service({requests.front().id,false,argv[2],{}});
    auto deadline=std::chrono::steady_clock::now()+5s;
    while(app.field(ui::Field::fast_status).text!="Saved complete received bytes."&&std::chrono::steady_clock::now()<deadline){app.tick();std::this_thread::sleep_for(20ms);}
    std::ifstream file(argv[2],std::ios::binary);std::string received((std::istreambuf_iterator<char>(file)),{});passed=received==message;
   }
  }
  std::cout<<"result="<<(passed?"PASS":"FAIL")<<" | "<<app.field(ui::Field::fast_status).text<<" | "<<app.field(ui::Field::fast_progress).text<<std::endl;
  app.close();while(!app.finished()){app.tick();std::this_thread::sleep_for(20ms);}return passed?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}
}
