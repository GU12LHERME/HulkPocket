package com.hulkpocket.diagnostic;
import android.app.Activity;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import android.graphics.Color;
import android.hardware.input.InputManager;
import java.io.File;

public final class GameActivity extends Activity implements SurfaceHolder.Callback,InputManager.InputDeviceListener {
    static {System.loadLibrary("hulkpocket_preview");}
    private static native void nativeSurface(Surface surface);
    private static native void nativeInput(boolean connected,int buttons,byte[] analog,int lx,int ly,int rx,int ry);
    private static native void nativeStop();
    private static native int nativeRun(String disc,String module,String partition,String log,String fvf);
    private boolean started=false,finished=false;
    private int device=-1,keys=0,hat=0,lx=0,ly=0,rx=0,ry=0;
    private final byte[] analog=new byte[8];
    private TextView status;
    private SurfaceView view;
    private InputManager input;
    private boolean pad(InputDevice d){return d!=null&&((d.getSources()&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||(d.getSources()&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK);}
    private void send(){nativeInput(device>=0,keys|hat,analog,lx,ly,rx,ry);}
    private void reset(){keys=hat=lx=ly=rx=ry=0;java.util.Arrays.fill(analog,(byte)0);send();}
    private void scan(){device=-1;for(int id:InputDevice.getDeviceIds())if(pad(InputDevice.getDevice(id))){device=id;break;}reset();}
    @Override public void onCreate(Bundle b){
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(5894);
        FrameLayout root=new FrameLayout(this);root.setBackgroundColor(Color.BLACK);
        view=new SurfaceView(this);view.getHolder().addCallback(this);
        root.addView(view,new FrameLayout.LayoutParams(1,1,Gravity.CENTER));
        root.addOnLayoutChangeListener((v,l,t,r,bottom,ol,ot,or,ob)->{
            int w=r-l,h=bottom-t;if(w<=0||h<=0)return;
            int sw=Math.min(w,h*4/3),sh=sw*3/4;
            FrameLayout.LayoutParams lp=(FrameLayout.LayoutParams)view.getLayoutParams();
            if(lp.width!=sw||lp.height!=sh){lp.width=sw;lp.height=sh;view.setLayoutParams(lp);}
        });
        status=new TextView(this);status.setText("Inicializando HulkPocket…");status.setTextColor(Color.WHITE);
        root.addView(status,new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.CENTER_HORIZONTAL));
        setContentView(root);input=(InputManager)getSystemService(INPUT_SERVICE);scan();
    }
    @Override public void surfaceCreated(SurfaceHolder holder){
        nativeSurface(holder.getSurface());
        if(started)return;started=true;
        String disc=getIntent().getStringExtra("disc"),module=getIntent().getStringExtra("module");
        File partition=new File(getFilesDir(),"preview-partition1");
        for(String dir:new String[]{"TDATA","UDATA"})new File(partition,dir+"/56550039").mkdirs();
        new Thread(()->{
            int code=nativeRun(disc,module,partition.getAbsolutePath(),new File(getFilesDir(),"preview-report.txt").getAbsolutePath(),getIntent().getStringExtra("fvf"));
            runOnUiThread(()->{finished=true;status.setVisibility(View.VISIBLE);status.setText("Execução encerrada ("+code+"). Volte ao launcher para exportar o relatório.");});
        },"hulk-game").start();
        status.postDelayed(()->{if(!finished)status.setVisibility(View.GONE);},3000);
    }
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int w,int h){}
    @Override public void surfaceDestroyed(SurfaceHolder holder){nativeSurface(null);}
    @Override public boolean dispatchKeyEvent(KeyEvent e){
        if(!pad(e.getDevice()))return super.dispatchKeyEvent(e);
        if(device!=e.getDeviceId()){device=e.getDeviceId();reset();}
        boolean down=e.getAction()==KeyEvent.ACTION_DOWN;
        int mask=0,index=-1;
        switch(e.getKeyCode()){
            case KeyEvent.KEYCODE_DPAD_UP:mask=1;break;case KeyEvent.KEYCODE_DPAD_DOWN:mask=2;break;
            case KeyEvent.KEYCODE_DPAD_LEFT:mask=4;break;case KeyEvent.KEYCODE_DPAD_RIGHT:mask=8;break;
            case KeyEvent.KEYCODE_BUTTON_START:mask=16;break;case KeyEvent.KEYCODE_BUTTON_SELECT:mask=32;break;
            case KeyEvent.KEYCODE_BUTTON_THUMBL:mask=64;break;case KeyEvent.KEYCODE_BUTTON_THUMBR:mask=128;break;
            case KeyEvent.KEYCODE_BUTTON_A:index=0;break;case KeyEvent.KEYCODE_BUTTON_B:index=1;break;
            case KeyEvent.KEYCODE_BUTTON_X:index=2;break;case KeyEvent.KEYCODE_BUTTON_Y:index=3;break;
            case KeyEvent.KEYCODE_BUTTON_R1:index=4;break;case KeyEvent.KEYCODE_BUTTON_L1:index=5;break;
            case KeyEvent.KEYCODE_BUTTON_L2:index=6;break;case KeyEvent.KEYCODE_BUTTON_R2:index=7;break;
            default:return super.dispatchKeyEvent(e);
        }
        if(mask!=0){if(down)keys|=mask;else keys&=~mask;}
        if(index>=0)analog[index]=(byte)(down?255:0);
        send();return true;
    }
    private int axis(MotionEvent e,int a,boolean invert){
        float x=e.getAxisValue(a);InputDevice.MotionRange r=e.getDevice().getMotionRange(a,e.getSource());
        if(r!=null&&Math.abs(x)<=Math.max(0.08f,r.getFlat()))x=0;
        if(invert)x=-x;return Math.round(Math.max(-1,Math.min(1,x))*32767);
    }
    private byte trigger(MotionEvent e,int a,int fallback,byte previous){
        if(e.getDevice().getMotionRange(a,e.getSource())==null)a=fallback;
        if(e.getDevice().getMotionRange(a,e.getSource())==null)return previous;
        return (byte)Math.round(Math.max(0,Math.min(1,e.getAxisValue(a)))*255);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent e){
        if(!pad(e.getDevice())||e.getAction()!=MotionEvent.ACTION_MOVE)return super.onGenericMotionEvent(e);
        if(device!=e.getDeviceId()){device=e.getDeviceId();reset();}
        lx=axis(e,MotionEvent.AXIS_X,false);ly=axis(e,MotionEvent.AXIS_Y,true);
        int ax=e.getDevice().getMotionRange(MotionEvent.AXIS_Z,e.getSource())!=null?MotionEvent.AXIS_Z:MotionEvent.AXIS_RX;
        int ay=e.getDevice().getMotionRange(MotionEvent.AXIS_RZ,e.getSource())!=null?MotionEvent.AXIS_RZ:MotionEvent.AXIS_RY;
        rx=axis(e,ax,false);ry=axis(e,ay,true);
        float hx=e.getAxisValue(MotionEvent.AXIS_HAT_X),hy=e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        hat=(hx<-.5?4:hx>.5?8:0)|(hy<-.5?1:hy>.5?2:0);
        analog[6]=trigger(e,MotionEvent.AXIS_LTRIGGER,MotionEvent.AXIS_BRAKE,analog[6]);
        analog[7]=trigger(e,MotionEvent.AXIS_RTRIGGER,MotionEvent.AXIS_GAS,analog[7]);send();return true;
    }
    @Override public void onInputDeviceAdded(int id){if(device<0)scan();}
    @Override public void onInputDeviceRemoved(int id){if(device==id)scan();}
    @Override public void onInputDeviceChanged(int id){if(device==id&&!pad(InputDevice.getDevice(id)))scan();}
    @Override protected void onResume(){super.onResume();if(input!=null)input.registerInputDeviceListener(this,null);scan();}
    @Override protected void onPause(){if(input!=null)input.unregisterInputDeviceListener(this);device=-1;reset();super.onPause();}
    @Override public void onBackPressed(){nativeStop();finish();}
    @Override protected void onDestroy(){nativeStop();nativeSurface(null);super.onDestroy();android.os.Process.killProcess(android.os.Process.myPid());}
}
