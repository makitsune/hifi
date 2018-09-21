package io.highfidelity.hifiinterface;

import android.app.Fragment;
import android.app.FragmentManager;
import android.app.FragmentTransaction;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.os.Bundle;
import android.support.annotation.NonNull;
import android.support.design.widget.NavigationView;
import android.support.v4.content.ContextCompat;
import android.support.v4.graphics.drawable.RoundedBitmapDrawable;
import android.support.v4.graphics.drawable.RoundedBitmapDrawableFactory;
import android.support.v4.view.GravityCompat;
import android.support.v4.widget.DrawerLayout;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import com.squareup.picasso.Callback;
import com.squareup.picasso.Picasso;

import io.highfidelity.hifiinterface.fragment.FriendsFragment;
import io.highfidelity.hifiinterface.fragment.HomeFragment;
import io.highfidelity.hifiinterface.fragment.LoginFragment;
import io.highfidelity.hifiinterface.fragment.PolicyFragment;
import io.highfidelity.hifiinterface.fragment.SignupFragment;
import io.highfidelity.hifiinterface.task.DownloadProfileImageTask;

public class MainActivity extends AppCompatActivity implements NavigationView.OnNavigationItemSelectedListener,
                                                                LoginFragment.OnLoginInteractionListener,
                                                                HomeFragment.OnHomeInteractionListener,
                                                                FriendsFragment.OnHomeInteractionListener,
                                                                SignupFragment.OnSignupInteractionListener {

    private static final int PROFILE_PICTURE_PLACEHOLDER = R.drawable.default_profile_avatar;
    public static final String DEFAULT_FRAGMENT = "Home";
    public static final String EXTRA_FRAGMENT = "fragment";
    public static final String EXTRA_BACK_TO_SCENE = "backToScene";

    private String TAG = "HighFidelity";

    public native boolean nativeIsLoggedIn();
    public native void nativeLogout();
    public native String nativeGetDisplayName();

    private DrawerLayout mDrawerLayout;
    private NavigationView mNavigationView;
    private ImageView mProfilePicture;
    private TextView mDisplayName;
    private View mLoginPanel;
    private View mProfilePanel;
    private TextView mLogoutOption;
    private MenuItem mPeopleMenuItem;

    private boolean backToScene;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mNavigationView = findViewById(R.id.nav_view);
        mNavigationView.setNavigationItemSelectedListener(this);

        mLoginPanel = mNavigationView.getHeaderView(0).findViewById(R.id.loginPanel);
        mProfilePanel = mNavigationView.getHeaderView(0).findViewById(R.id.profilePanel);

        mLogoutOption = mNavigationView.findViewById(R.id.logout);

        mDisplayName = mNavigationView.getHeaderView(0).findViewById(R.id.displayName);
        mProfilePicture = mNavigationView.getHeaderView(0).findViewById(R.id.profilePicture);

        mPeopleMenuItem = mNavigationView.getMenu().findItem(R.id.action_people);

        Toolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setTitleTextAppearance(this, R.style.HomeActionBarTitleStyle);
        setSupportActionBar(toolbar);

        ActionBar actionbar = getSupportActionBar();
        actionbar.setDisplayHomeAsUpEnabled(true);
        actionbar.setHomeAsUpIndicator(R.drawable.ic_menu);

        mDrawerLayout = findViewById(R.id.drawer_layout);

        Window window = getWindow();
        window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        window.setStatusBarColor(ContextCompat.getColor(this, R.color.statusbar_color));

        if (getIntent() != null) {
            if (getIntent().hasExtra(EXTRA_FRAGMENT)) {
                loadFragment(getIntent().getStringExtra(EXTRA_FRAGMENT));
            } else {
                loadFragment(DEFAULT_FRAGMENT);
            }

            if (getIntent().hasExtra(EXTRA_BACK_TO_SCENE)) {
                backToScene = getIntent().getBooleanExtra(EXTRA_BACK_TO_SCENE, false);
            }
        }
    }

    private void loadFragment(String fragment) {
        switch (fragment) {
            case "Login":
                loadLoginFragment();
                break;
            case "Home":
                loadHomeFragment(true);
                break;
            case "Privacy Policy":
                loadPrivacyPolicyFragment();
                break;
            case "People":
                loadPeopleFragment();
                break;
            default:
                Log.e(TAG, "Unknown fragment " + fragment);
        }

    }

    private void loadHomeFragment(boolean addToBackStack) {
        Fragment fragment = HomeFragment.newInstance();
        loadFragment(fragment, getString(R.string.home), getString(R.string.tagFragmentHome), addToBackStack);
    }

    private void loadLoginFragment() {
        Fragment fragment = LoginFragment.newInstance();

        loadFragment(fragment, getString(R.string.login), getString(R.string.tagFragmentLogin), true);
    }

    private void loadSignupFragment() {
        Fragment fragment = SignupFragment.newInstance();

        loadFragment(fragment, getString(R.string.signup), getString(R.string.tagFragmentSignup), true);
    }

    private void loadPrivacyPolicyFragment() {
        Fragment fragment = PolicyFragment.newInstance();

        loadFragment(fragment, getString(R.string.privacyPolicy), getString(R.string.tagFragmentPolicy), true);
    }

    private void loadPeopleFragment() {
        Fragment fragment = FriendsFragment.newInstance();

        loadFragment(fragment, getString(R.string.people), getString(R.string.tagFragmentPeople), true);
    }

    private void loadFragment(Fragment fragment, String title, String tag, boolean addToBackStack) {
        FragmentManager fragmentManager = getFragmentManager();

        // check if it's the same fragment
        String currentFragmentName = fragmentManager.getBackStackEntryCount() > 0
                ? fragmentManager.getBackStackEntryAt(fragmentManager.getBackStackEntryCount() - 1).getName()
                : "";
        if (currentFragmentName.equals(title)) {
            mDrawerLayout.closeDrawer(mNavigationView);
            return; // cancel as we are already in that fragment
        }

        // go back until first transaction
        int backStackEntryCount = fragmentManager.getBackStackEntryCount();
        for (int i = 0; i < backStackEntryCount - 1; i++) {
            fragmentManager.popBackStackImmediate();
        }

        // this case is when we wanted to go home.. rollback already did that!
        // But asking for a new Home fragment makes it easier to have an updated list so we let it to continue

        FragmentTransaction ft = fragmentManager.beginTransaction();
        ft.replace(R.id.content_frame, fragment, tag);

        if (addToBackStack) {
            ft.addToBackStack(title);
        }
        ft.commit();
        setTitle(title);
        mDrawerLayout.closeDrawer(mNavigationView);
    }

    
    private void updateLoginMenu() {
        if (nativeIsLoggedIn()) {
            mLoginPanel.setVisibility(View.GONE);
            mProfilePanel.setVisibility(View.VISIBLE);
            mLogoutOption.setVisibility(View.VISIBLE);
            mPeopleMenuItem.setVisible(true);
            updateProfileHeader();
        } else {
            mLoginPanel.setVisibility(View.VISIBLE);
            mProfilePanel.setVisibility(View.GONE);
            mLogoutOption.setVisibility(View.GONE);
            mPeopleMenuItem.setVisible(false);
            mDisplayName.setText("");
        }
    }

    private void updateProfileHeader() {
        updateProfileHeader(nativeGetDisplayName());
    }
    private void updateProfileHeader(String username) {
        if (!username.isEmpty()) {
            mDisplayName.setText(username);
            updateProfilePicture(username);
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        //getMenuInflater().inflate(R.menu.menu_navigation, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();
        switch (id) {
            case android.R.id.home:
                mDrawerLayout.openDrawer(GravityCompat.START);
                return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    public boolean onNavigationItemSelected(@NonNull MenuItem item) {
        switch(item.getItemId()) {
            case R.id.action_home:
                loadHomeFragment(false);
                return true;
            case R.id.action_people:
                loadPeopleFragment();
                return true;
        }
        return false;
    }

    @Override
    protected void onStart() {
        super.onStart();
        updateLoginMenu();
    }

    public void onLoginClicked(View view) {
        loadLoginFragment();
    }

    public void onLogoutClicked(View view) {
        nativeLogout();
        updateLoginMenu();
        exitLoggedInFragment();

    }

    private void exitLoggedInFragment() {
        // If we are in a "logged in" fragment (like People), go back to home. This could be expanded to multiple fragments
        FragmentManager fragmentManager = getFragmentManager();
        String currentFragmentName = fragmentManager.getBackStackEntryCount() > 0
                ? fragmentManager.getBackStackEntryAt(fragmentManager.getBackStackEntryCount() - 1).getName()
                : "";
        if (currentFragmentName.equals(getString(R.string.people))) {
            loadHomeFragment(false);
        }
    }

    public void onSelectedDomain(String domainUrl) {
        goToDomain(domainUrl);
    }

    private void goToLastLocation() {
        goToDomain("");
    }

    private void goToDomain(String domainUrl) {
        Intent intent = new Intent(this, InterfaceActivity.class);
        intent.putExtra(InterfaceActivity.DOMAIN_URL, domainUrl);
        finish();
        intent.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
    }

    private void goToUser(String username) {
        Intent intent = new Intent(this, InterfaceActivity.class);
        intent.putExtra(InterfaceActivity.EXTRA_GOTO_USERNAME, username);
        finish();
        intent.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
    }

    @Override
    public void onLoginCompleted() {
        loadHomeFragment(false);
        updateLoginMenu();
        if (backToScene) {
            backToScene = false;
            goToLastLocation();
        }
    }

    @Override
    public void onLoginRequested() {
        loadLoginFragment();
    }

    @Override
    public void onSignupRequested() {
        loadSignupFragment();
    }

    @Override
    public void onSignupCompleted() {
        Toast.makeText(this, "Sign up succeeded", Toast.LENGTH_SHORT).show();
        loadLoginFragment();
    }

    public void handleUsernameChanged(String username) {
        runOnUiThread(() -> updateProfileHeader(username));
    }

    /**
     * This is a temporary way to get the profile picture URL
     * TODO: this should be get from an API (at the moment there is no one for this)
     */
    private void updateProfilePicture(String username) {
        mProfilePicture.setImageResource(PROFILE_PICTURE_PLACEHOLDER);
        new DownloadProfileImageTask(url ->  { if (url != null && !url.isEmpty()) {
                Picasso.get().load(url).into(mProfilePicture, new RoundProfilePictureCallback());
            } } ).execute(username);
    }

    public void onPrivacyPolicyClicked(View view) {
        loadPrivacyPolicyFragment();
    }

    @Override
    public void onVisitUserSelected(String username) {
        goToUser(username);
    }

    private class RoundProfilePictureCallback implements Callback {
        @Override
        public void onSuccess() {
            Bitmap imageBitmap = ((BitmapDrawable) mProfilePicture.getDrawable()).getBitmap();
            RoundedBitmapDrawable imageDrawable = RoundedBitmapDrawableFactory.create(getResources(), imageBitmap);
            imageDrawable.setCircular(true);
            imageDrawable.setCornerRadius(Math.max(imageBitmap.getWidth(), imageBitmap.getHeight()) / 2.0f);
            mProfilePicture.setImageDrawable(imageDrawable);
        }

        @Override
        public void onError(Exception e) {
            mProfilePicture.setImageResource(PROFILE_PICTURE_PLACEHOLDER);
        }
    }

    @Override
    public void onBackPressed() {
        // if a fragment needs to internally manage back presses..
        FragmentManager fm = getFragmentManager();
        Log.d("[BACK]", "getBackStackEntryCount " + fm.getBackStackEntryCount());
        Fragment friendsFragment = fm.findFragmentByTag(getString(R.string.tagFragmentPeople));
        if (friendsFragment != null && friendsFragment instanceof FriendsFragment) {
            if (((FriendsFragment) friendsFragment).onBackPressed()) {
                return;
            }
        }

        int index = fm.getBackStackEntryCount() - 1;

        if (index > 0) {
            super.onBackPressed();
            index--;
            if (index > -1) {
                setTitle(fm.getBackStackEntryAt(index).getName());
            }
            if (backToScene) {
                backToScene = false;
                goToLastLocation();
            }
        } else {
            finishAffinity();
        }
    }

    @Override
    public void onSaveInstanceState(Bundle savedInstanceState) {
        super.onSaveInstanceState(savedInstanceState);
        savedInstanceState.putBoolean(EXTRA_BACK_TO_SCENE, backToScene);
    }

    @Override
    protected void onRestoreInstanceState(Bundle savedInstanceState) {
        super.onRestoreInstanceState(savedInstanceState);
        backToScene = savedInstanceState.getBoolean(EXTRA_BACK_TO_SCENE, false);
    }
}
