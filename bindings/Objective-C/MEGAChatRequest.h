#import <Foundation/Foundation.h>
#import "MEGAChatPeerList.h"

typedef NS_ENUM (NSInteger, MEGAChatRequestType) {
    MEGAChatRequestTypeInitialize,
    MEGAChatRequestTypeConnect,
    MEGAChatRequestTypeDelete,
    MEGAChatRequestTypeLogout,
    MEGAChatRequestTypeSetOnlineStatus,
    MEGAChatRequestTypeStartChatCall,
    MEGAChatRequestTypeAnswerChatCall,
    MEGAChatRequestTypeMuteChatCall,
    MEGAChatRequestTypeHangChatCall,
    MEGAChatRequestTypeCreateChatRoom,
    MEGAChatRequestTypeRemoveFromChatRoom,
    MEGAChatRequestTypeInviteToChatRoom,
    MEGAChatRequestTypeUpdatePeerPermissions,
    MEGAChatRequestTypeEditChatRoomName,
    MEGAChatRequestTypeEditChatRoomPic,
    MEGAChatRequestTypeTruncateHistory,
    MEGAChatRequestTypeShareContact,
    MEGAChatRequestTypeGetFirstname,
    MEGAChatRequestTypeGetLastname,
    MEGAChatRequestTypeDisconnect,
    MEGAChatRequestTypeGetEmail
};

@interface MEGAChatRequest : NSObject

@property (readonly, nonatomic) MEGAChatRequestType type;
@property (readonly, nonatomic) NSString *requestString;
@property (readonly, nonatomic) NSInteger tag;
@property (readonly, nonatomic) long long number;
@property (readonly, nonatomic, getter=isFlag) BOOL flag;
@property (readonly, nonatomic) MEGAChatPeerList *megaChatPeerList;
@property (readonly, nonatomic) uint64_t chatHandle;
@property (readonly, nonatomic) uint64_t userHandle;
@property (readonly, nonatomic) NSInteger privilege;
@property (readonly, nonatomic) NSString *text;

- (instancetype)clone;

@end
