package com.akinalpfdn.osdrop.storage

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.GeneralSecurityException
import java.security.KeyStore as JavaKeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Why the identity or the group could not be read or stored. */
class SecureStoreException(message: String) : Exception(message)

/** Where this phone's identity and its group live; a fake in tests. */
interface SecureStoring {
    /** The 32-byte secret key, created and stored on first use by [create]. */
    fun secretKey(create: () -> ByteArray): ByteArray

    /** The group state core handed over last; null before the first group. */
    fun groupState(): ByteArray?

    fun saveGroupState(state: ByteArray)
}

/**
 * The identity's secret key and the group state, each encrypted with an AES key that never leaves
 * the Android Keystore, in app-private preferences (as Osplay's Android app keeps its identity).
 */
class AndroidSecureStore(context: Context) : SecureStoring {
    private val preferences: SharedPreferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)

    override fun secretKey(create: () -> ByteArray): ByteArray {
        preferences.getString(IDENTITY, null)?.let { return decrypt(it) }
        val created = create()
        preferences.edit().putString(IDENTITY, encrypt(created)).apply()
        return created
    }

    override fun groupState(): ByteArray? = preferences.getString(GROUP, null)?.let(::decrypt)

    override fun saveGroupState(state: ByteArray) {
        preferences.edit().putString(GROUP, encrypt(state)).apply()
    }

    private fun encrypt(secret: ByteArray): String = guarded {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, wrappingKey())
        Base64.encodeToString(cipher.iv + cipher.doFinal(secret), Base64.NO_WRAP)
    }

    private fun decrypt(stored: String): ByteArray = guarded {
        val sealed = Base64.decode(stored, Base64.NO_WRAP)
        if (sealed.size <= IV_BYTES) throw SecureStoreException("damaged")
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, wrappingKey(), GCMParameterSpec(TAG_BITS, sealed, 0, IV_BYTES))
        cipher.doFinal(sealed, IV_BYTES, sealed.size - IV_BYTES)
    }

    private fun wrappingKey(): SecretKey {
        val keyStore = JavaKeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(KEY_BITS)
                .build(),
        )
        return generator.generateKey()
    }

    private inline fun <T> guarded(block: () -> T): T = try {
        block()
    } catch (error: GeneralSecurityException) {
        throw SecureStoreException(error.javaClass.simpleName)
    } catch (error: IllegalArgumentException) {
        throw SecureStoreException("damaged")
    }

    private companion object {
        const val PREFERENCES = "osdrop.secure"
        const val IDENTITY = "identity"
        const val GROUP = "group"
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "com.akinalpfdn.osdrop.identity"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_BYTES = 12
        const val TAG_BITS = 128
        const val KEY_BITS = 256
    }
}
