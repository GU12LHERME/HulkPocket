package com.hulkpocket.diagnostic;
import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.widget.*;
import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

public class MainActivity extends Activity {
 TextView status; Button select;
 @Override public void onCreate(Bundle b) {
  super.onCreate(b);
  LinearLayout layout=new LinearLayout(this); layout.setOrientation(1); layout.setPadding(32,24,32,24);
  TextView title=new TextView(this); title.setText("HulkPocket — Diagnóstico"); title.setTextSize(24); layout.addView(title);
  select=new Button(this); select.setText("Selecionar ISO Xbox"); layout.addView(select);
  status=new TextView(this); status.setText("Selecione a imagem original do jogo."); status.setTextIsSelectable(true);
  ScrollView scroll=new ScrollView(this); scroll.addView(status); layout.addView(scroll); setContentView(layout);
  select.setOnClickListener(v->{Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT); i.setType("*/*"); i.addCategory(Intent.CATEGORY_OPENABLE); startActivityForResult(i,1);});
 }
 @Override protected void onActivityResult(int request,int result,Intent data) {
  super.onActivityResult(request,result,data);
  if(request!=1||result!=RESULT_OK||data==null)return;
  Uri uri=data.getData(); select.setEnabled(false); status.setText("Analisando...");
  new Thread(()->{String report; try(ParcelFileDescriptor fd=getContentResolver().openFileDescriptor(uri,"r"); FileInputStream in=new FileInputStream(fd.getFileDescriptor())){
   report=inspect(in.getChannel());
  }catch(Exception e){report="Erro: "+e.getMessage();}
  final String output=report; runOnUiThread(()->{status.setText(output);select.setEnabled(true);});}).start();
 }
 static ByteBuffer read(java.nio.channels.FileChannel c,long pos,int n)throws IOException{
  if(pos<0||n<0||pos>c.size()-n)throw new IOException("Leitura fora da imagem");
  ByteBuffer b=ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN); c.position(pos);
  while(b.hasRemaining())if(c.read(b)<=0)throw new EOFException(); b.flip(); return b;
 }
 static long u32(ByteBuffer b,int p){return Integer.toUnsignedLong(b.getInt(p));}
 static String inspect(java.nio.channels.FileChannel c)throws Exception{
  long base=-1; ByteBuffer h=null;
  for(long candidate:new long[]{0,0xFD90000L}){
   if(c.size()<candidate+33*2048L)continue;
   ByteBuffer t=read(c,candidate+32*2048L,2048);
   if(new String(t.array(),0,20,StandardCharsets.US_ASCII).equals("MICROSOFT*XBOX*MEDIA")){base=candidate;h=t;break;}
  }
  if(h==null)throw new IOException("Formato XISO não reconhecido");
  long size=u32(h,24); if(size<14||size>16777216)throw new IOException("Diretório inválido");
  ByteBuffer d=read(c,base+u32(h,20)*2048L,(int)size);
  ArrayDeque<Integer> q=new ArrayDeque<>(); HashSet<Integer> seen=new HashSet<>(); q.add(0);
  while(!q.isEmpty()){
   int p=q.remove(); if(!seen.add(p)||p>d.limit()-14)throw new IOException("Entrada inválida");
   int n=Byte.toUnsignedInt(d.get(p+13)); if(p+14+n>d.limit())throw new IOException("Nome inválido");
   String name=new String(d.array(),p+14,n,StandardCharsets.US_ASCII);
   if(name.equalsIgnoreCase("default.xbe")&&(d.get(p+12)&16)==0){
    long count=u32(d,p+8), start=base+u32(d,p+4)*2048L;
    if(count<284||count>134217728||start>c.size()-count)throw new IOException("XBE inválido");
    ByteBuffer x=read(c,start,284);
    if(x.getInt(0)!=0x48454258)throw new IOException("Assinatura XBE inválida");
    long cert=u32(x,0x118)-u32(x,0x104);
    if(cert<0||cert>count-176)throw new IOException("Certificado inválido");
    ByteBuffer a=read(c,start+cert,176);
    String title=new String(a.array(),12,80,StandardCharsets.UTF_16LE).split("\\x00",2)[0];
    MessageDigest md=MessageDigest.getInstance("SHA-256");
    for(long off=0;off<count;){int chunk=(int)Math.min(1048576,count-off);md.update(read(c,start+off,chunk));off+=chunk;}
    StringBuilder hash=new StringBuilder();for(byte v:md.digest())hash.append(String.format(Locale.ROOT,"%02x",v&255));
    return String.format(Locale.ROOT,"Jogo: %s%nTitle ID: %08X%nRegião (máscara): %d%nXBE: %d bytes%nSHA-256: %s%n%nCompatibilidade com o motor ainda não verificada.",title,u32(a,8),u32(a,0xa0),count,hash);
   }
   for(int k:new int[]{0,2}){int child=Short.toUnsignedInt(d.getShort(p+k));if(child!=0)q.add(child*4);}
  }
  throw new IOException("default.xbe não encontrado");
 }
}
