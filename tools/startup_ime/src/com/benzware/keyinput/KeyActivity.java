package com.benzware.keyinput;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** A real focused Android editor. The app has no permissions or launcher icon. */
public final class KeyActivity extends Activity {
    private String session;
    private String expected;
    private EditText entry;
    private boolean finished;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        session = getIntent().getStringExtra("session");
        expected = getIntent().getStringExtra("expected");
        if (session == null || !session.matches("[0-9a-f]{32}") ||
                expected == null || expected.length() > 64) {
            finished = true;
            finish();
            return;
        }
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE |
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        int pad = (int) (20 * getResources().getDisplayMetrics().density + 0.5f);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("Enter key:");
        title.setTextSize(20);
        box.addView(title);
        entry = new EditText(this);
        entry.setId(0x42);
        entry.setSingleLine(true);
        entry.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        entry.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI |
                EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING);
        entry.setFilters(new InputFilter[] { new InputFilter.LengthFilter(64) });
        // Do not let a physical Enter key move focus to the Cancel button.
        // Soft-keyboard actionDone still arrives through OnEditorActionListener.
        entry.setOnKeyListener(new View.OnKeyListener() {
            @Override public boolean onKey(View view, int keyCode, KeyEvent event) {
                if (keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER) {
                    if (event.getAction() == KeyEvent.ACTION_UP) submit();
                    return true;
                }
                return false;
            }
        });
        entry.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override public boolean onEditorAction(TextView view, int actionId, KeyEvent event) {
                if (actionId == EditorInfo.IME_ACTION_DONE ||
                        (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER &&
                                event.getAction() == KeyEvent.ACTION_UP)) {
                    submit();
                    return true;
                }
                return false;
            }
        });
        box.addView(entry, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        Button cancel = new Button(this);
        cancel.setText("Cancel");
        cancel.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { KeyActivity.this.cancel(); }
        });
        Button enter = new Button(this);
        enter.setText("Enter");
        enter.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { submit(); }
        });
        actions.addView(cancel, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(enter, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        box.addView(actions);
        setContentView(box);
        entry.requestFocus();
        // The native process waits for this handshake; no result from an old
        // invocation can match the fresh 128-bit session name.
        if (!writeAtomically("ready-" + session, "ready")) {
            finished = true;
            finish();
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && entry != null && !finished) {
            entry.requestFocus();
            entry.post(new Runnable() {
                @Override public void run() {
                    if (!hasWindowFocus() || finished) return;
                    InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (imm != null) imm.showSoftInput(entry, InputMethodManager.SHOW_IMPLICIT);
                }
            });
        }
    }

    private void submit() {
        if (finished) return;
        String value = entry.getText().toString();
        android.util.Log.i("BWInput", "submit matches=" + expected.equals(value) + " length=" + value.length());
        if (!expected.equals(value)) {
            entry.setError("Invalid key");
            entry.selectAll();
            return;
        }
        if (!writeAtomically("result-" + session, "T" + value)) {
            entry.setError("Unable to send key");
            return;
        }
        finished = true;
        finish();
    }

    private void cancel() {
        if (finished) return;
        android.util.Log.i("BWInput", "cancel");
        if (!writeAtomically("result-" + session, "C")) {
            if (entry != null) entry.setError("Unable to cancel");
            return;
        }
        finished = true;
        finish();
    }

    @Override public void onBackPressed() { cancel(); }

    @Override public void onStop() {
        super.onStop();
        android.util.Log.i("BWInput", "stop finished=" + finished + " changing=" + isChangingConfigurations());
        // Leaving the task should not leave the native launcher waiting. A
        // configuration change recreates the editor and must not cancel it.
        if (!finished && !isChangingConfigurations()) cancel();
    }

    private boolean writeAtomically(String name, String value) {
        File dir = getFilesDir();
        File tmp = new File(dir, name + ".tmp");
        File dst = new File(dir, name);
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(value.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch (IOException e) {
            tmp.delete();
            return false;
        }
        if (!tmp.renameTo(dst)) {
            tmp.delete();
            return false;
        }
        return true;
    }
}
