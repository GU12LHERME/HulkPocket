package com.hulkpocket.diagnostic;
import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.*;
import java.io.*;
import java.util.UUID;
import java.util.Properties;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipEntry;
import java.security.MessageDigest;

public final class PreviewLauncherActivity extends Activity {
    private TextView status;
    private Button discButton,moduleButton,playButton;
    private volatile boolean busy;
    private File root(){return getFilesDir();}
    private File disc(){return new File(root(),getPreferences(0).getString("disc","missing"));}
    private File module(){return new File(root(),getPreferences(0).getString("module","missing"));}
    private void update(){
        boolean d=new File(disc(),"default.xbe").isFile(),m=module().isFile()&&
            BuildConfig.PREVIEW_REVISION.equals(getPreferences(0).getString("moduleRevision",""));
        boolean matched=m&&d&&getPreferences(0).getString("discSha","").equals(getPreferences(0).getString("moduleXbe",""))&&
            !getPreferences(0).getString("discSha","").isEmpty();
        status.setText("Versão experimental • USA\nJogo: "+(d?"importado":"não importado")+"\nMódulo ARM64: "+(m?"importado":"não importado"));
        discButton.setEnabled(!busy);moduleButton.setEnabled(!busy);playButton.setEnabled(!busy&&matched);
        if(d&&m&&!matched)status.append("\nMódulo não corresponde ao XBE importado.");
    }
    @Override public void onCreate(Bundle b){
        super.onCreate(b);
        LinearLayout panel=new LinearLayout(this);panel.setOrientation(1);panel.setPadding(40,24,40,24);panel.setBackgroundColor(0xff000000);
        TextView title=new TextView(this);title.setText("HulkPocket");title.setTextSize(30);panel.addView(title);
        status=new TextView(this);panel.addView(status);
        discButton=new Button(this);discButton.setText("Importar pasta do jogo");panel.addView(discButton);
        moduleButton=new Button(this);moduleButton.setText("Importar HulkPocket-module.zip");panel.addView(moduleButton);
        playButton=new Button(this);playButton.setText("Jogar");panel.addView(playButton);
        Button report=new Button(this);report.setText("Exportar último relatório");panel.addView(report);
        report.setOnClickListener(v->{
            File f=new File(root(),"preview-report.txt");
            if(!f.isFile()){status.setText("Nenhum relatório disponível.");return;}
            Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.setType("text/plain");i.putExtra(Intent.EXTRA_TITLE,"HulkPocket-preview.txt");startActivityForResult(i,12);
        });
        ScrollView scroll=new ScrollView(this);scroll.addView(panel);setContentView(scroll);
        discButton.setOnClickListener(v->startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE),10));
        moduleButton.setOnClickListener(v->new android.app.AlertDialog.Builder(this).setTitle("Módulo nativo local")
            .setMessage("Importe apenas o módulo ARM64 que você recompilou a partir da sua própria cópia e código confiável. Um módulo nativo executa código no aplicativo.")
            .setNegativeButton("Cancelar",null).setPositiveButton("Selecionar",(a,c)->{
                Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.setType("*/*");i.addCategory(Intent.CATEGORY_OPENABLE);startActivityForResult(i,11);
            }).show());
        playButton.setOnClickListener(v->{
            Intent i=new Intent(this,GameActivity.class);i.putExtra("disc",disc().getAbsolutePath());i.putExtra("module",module().getAbsolutePath());
            i.putExtra("fvf",getPreferences(0).getString("moduleFvf",""));startActivity(i);
        });
        update();
    }
    private static void remove(File f){
        File[] children=f.listFiles();if(children!=null)for(File child:children)remove(child);f.delete();
    }
    private static String safe(String name)throws IOException{
        if(name==null||name.isEmpty()||name.equals(".")||name.equals("..")||name.contains("/")||name.contains("\\")||name.indexOf(0)>=0)throw new IOException("Nome inválido");
        return name;
    }
    private long copied;private int files;
    private void copy(Uri uri,File out,long max,boolean readOnly)throws IOException{
        if(out.exists())throw new IOException("Arquivo duplicado");
        try(InputStream in=getContentResolver().openInputStream(uri);FileOutputStream dst=new FileOutputStream(out)){
            if(in==null)throw new IOException("Arquivo inacessível");
            if(readOnly&&!out.setReadOnly())throw new IOException("Não foi possível proteger módulo");
            byte[] buf=new byte[131072];int n;long count=0;
            while((n=in.read(buf))>0){count+=n;copied+=n;if(count>max||copied>10L*1024*1024*1024)throw new IOException("Limite de importação");dst.write(buf,0,n);}
            dst.getFD().sync();
        }
    }
    private void tree(Uri root,String id,File dir,int depth)throws IOException{
        if(depth>24||!dir.mkdir())throw new IOException("Diretório inválido");
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(root,id);
        String[] columns={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE};
        try(Cursor c=getContentResolver().query(children,columns,null,null,null)){
            if(c==null)throw new IOException("Pasta inacessível");
            while(c.moveToNext()){
                if(++files>20000)throw new IOException("Arquivos demais");
                String child=c.getString(0),name=safe(c.getString(1));File target=new File(dir,name);
                if(DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2)))tree(root,child,target,depth+1);
                else copy(DocumentsContract.buildDocumentUriUsingTree(root,child),target,5L*1024*1024*1024,false);
            }
        }
    }
    private static String sha(File f)throws Exception{
        MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(InputStream in=new FileInputStream(f)){byte[] b=new byte[131072];int n;while((n=in.read(b))>0)digest.update(b,0,n);}
        StringBuilder hex=new StringBuilder();for(byte v:digest.digest())hex.append(String.format(java.util.Locale.ROOT,"%02x",v&255));
        return hex.toString();
    }
    private Properties importBundle(Uri uri,File target)throws Exception{
        Properties p=new Properties();
        boolean meta=false,code=false;
        try(InputStream raw=getContentResolver().openInputStream(uri)){
            if(raw==null)throw new IOException("Pacote inacessível");
            try(ZipInputStream z=new ZipInputStream(raw)){
                ZipEntry e;byte[] b=new byte[131072];
                while((e=z.getNextEntry())!=null){
                    if(e.isDirectory())throw new IOException("Entrada de pacote inválida");
                    if(e.getName().equals("preview.properties")&&!meta){
                        ByteArrayOutputStream data=new ByteArrayOutputStream();int n;
                        while((n=z.read(b))>0){if(data.size()+n>4096)throw new IOException("Metadados muito grandes");data.write(b,0,n);}
                        p.load(new ByteArrayInputStream(data.toByteArray()));meta=true;
                    }else if(e.getName().equals("libhulk-code.so")&&!code){
                        try(FileOutputStream out=new FileOutputStream(target)){
                            if(!target.setReadOnly())throw new IOException("Não foi possível proteger módulo");
                            long count=0;int n;while((n=z.read(b))>0){count+=n;if(count>512L*1024*1024)throw new IOException("Módulo muito grande");out.write(b,0,n);}
                            out.getFD().sync();
                        }
                        code=true;
                    }else throw new IOException("Entrada desconhecida ou duplicada");
                    z.closeEntry();
                }
            }
        }
        if(!meta||!code||!"1".equals(p.getProperty("format"))||!BuildConfig.PREVIEW_REVISION.equals(p.getProperty("revision"))||
           !p.getProperty("fvf","").matches("[0-9A-Fa-f]{8}")||!p.getProperty("xbe_sha256","").matches("[0-9a-f]{64}")||
           !p.getProperty("module_sha256","").equals(sha(target)))throw new IOException("Pacote inválido ou de revisão diferente do APK");
        byte[] h=new byte[20];try(DataInputStream in=new DataInputStream(new FileInputStream(target))){in.readFully(h);}
        if(h[0]!=127||h[1]!='E'||h[2]!='L'||h[3]!='F'||h[4]!=2||h[5]!=1||h[16]!=3||h[17]!=0||(h[18]&255)!=183||h[19]!=0)throw new IOException("Módulo não é ELF ARM64 compartilhado");
        return p;
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK||data==null||data.getData()==null||busy)return;
        Uri uri=data.getData();
        if(request==12){
            try(InputStream in=new FileInputStream(new File(root(),"preview-report.txt"));OutputStream out=getContentResolver().openOutputStream(uri,"wt")){
                if(out==null)throw new IOException("Destino inacessível");
                byte[] buf=new byte[8192];int n;while((n=in.read(buf))>0)out.write(buf,0,n);status.setText("Relatório exportado.");
            }catch(IOException ex){status.setText(ex.toString());}return;
        }
        if(request!=10&&request!=11)return;
        busy=true;update();status.setText("Importando arquivos locais...");
        new Thread(()->{
            File target=new File(root(),(request==10?"disc-":"module-")+UUID.randomUUID());String error=null;
            try{
                copied=0;files=0;
                android.content.SharedPreferences.Editor edit=getPreferences(0).edit();
                if(request==10){
                    tree(uri,DocumentsContract.getTreeDocumentId(uri),target,0);
                    File xbe=new File(target,"default.xbe");
                    if(!xbe.isFile())throw new IOException("Selecione a pasta que contém default.xbe");
                    edit.putString("discSha",sha(xbe));
                }else{
                    Properties p=importBundle(uri,target);
                    edit.putString("moduleRevision",p.getProperty("revision"))
                        .putString("moduleXbe",p.getProperty("xbe_sha256")).putString("moduleFvf",p.getProperty("fvf"));
                }
                String key=request==10?"disc":"module";
                String old=getPreferences(0).getString(key,"");
                if(!edit.putString(key,target.getName()).commit())throw new IOException("Falha ao salvar seleção");
                if(old.startsWith(request==10?"disc-":"module-"))remove(new File(root(),old));
            }catch(Exception ex){error=ex.getMessage();remove(target);}
            final String message=error;runOnUiThread(()->{busy=false;update();if(message!=null)status.setText("Erro: "+message);});
        },"hulk-import").start();
    }
}
